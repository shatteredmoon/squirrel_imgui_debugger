/*

Squirrel ImGui Debugger VM Manager

MIT License

Copyright 2022 Jonathon Blake Wood-Brooks

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <d_vm.h>

#include <d_interface.h>
#include <d_settings.h>
#include <d_utility.h>

#include <fstream>
#include <regex>
#include <sstream>

#if DEBUG_OUTPUT == 0
#define assert(x)
#endif // DEBUG_OUTPUT

#include <sqconfig.h>
#include <sqobject.h>
#include <sqstring.h>

#if DEBUG_OUTPUT == 0
#undef assert
#endif // DEBUG_OUTPUT

#define SQ_LINEEXECUTION  'l'
#define SQ_FUNCTIONCALL   'c'
#define SQ_FUNCTIONRETURN 'r'

std::vector<rumDebugContext> rumDebugVM::s_vDebugContexts;
std::string rumDebugVM::s_strAttachRequest;
std::string rumDebugVM::s_strDetachRequest;
std::map<HSQUIRRELVM, std::string> rumDebugVM::s_mapRegisteredVMs;
std::vector<rumDebugBreakpoint> rumDebugVM::s_vBreakpoints;
std::map<std::string, rumDebugFile> rumDebugVM::s_mapOpenedFiles;
rumDebugContext* rumDebugVM::s_vCurrentDebugContext{ nullptr };
HSQUIRRELCONSTVM rumDebugVM::s_pCurrentVM{ nullptr };
uint32_t rumDebugVM::s_uiLocalVariableStackLevel{ 0 };
std::vector<rumDebugVariable> rumDebugVM::s_vLocalVariables;
std::vector<rumDebugVariable> rumDebugVM::s_vWatchVariables;
std::vector<rumDebugVariable> rumDebugVM::s_vRequestedVariables;
std::mutex rumDebugVM::s_mtxAccessLock;
std::mutex rumDebugVM::s_mtxDebugLock;
std::condition_variable rumDebugVM::s_cvDebugLock;


// static
SQInteger rumDebugVM::AttachVM( HSQUIRRELVM i_pVM )
{
  const auto& iter{ std::find( s_vDebugContexts.begin(), s_vDebugContexts.end(), i_pVM ) };
  if( iter == s_vDebugContexts.end() )
  {
    rumDebugContext cContext( i_pVM );
    sq_setnativedebughook( i_pVM, &NativeDebugHook );
    s_vDebugContexts.emplace_back( std::move( cContext ) );
    
    if( !s_pCurrentVM )
    {
      s_pCurrentVM = i_pVM;
    }

    return SQ_OK;
  }

  return SQ_ERROR;
}


// static
void rumDebugVM::AttachVM( const std::string& i_strName )
{
  HSQUIRRELVM pVM{ GetVMByName( i_strName ) };
  if( pVM )
  {
    AttachVM( pVM );
  }
}


// static
void rumDebugVM::BreakpointAdd( rumDebugBreakpoint i_cBreakpoint )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  s_vBreakpoints.emplace_back( std::move( i_cBreakpoint ) );

  rumDebugInterface::RequestSettingsUpdate();
}


// static
void rumDebugVM::BreakpointRemove( const rumDebugBreakpoint& i_rcBreakpoint )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  auto iter{ std::find( s_vBreakpoints.begin(), s_vBreakpoints.end(), i_rcBreakpoint ) };
  if( iter != s_vBreakpoints.end() )
  {
    // Remove the existing breakpoint
    s_vBreakpoints.erase( iter );
    rumDebugInterface::RequestSettingsUpdate();
  }
}


// static
void rumDebugVM::BreakpointToggle( const rumDebugBreakpoint& i_rcBreakpoint )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  auto iter{ std::find( s_vBreakpoints.begin(), s_vBreakpoints.end(), i_rcBreakpoint ) };
  if( s_vBreakpoints.end() == iter )
  {
    // Breakpoint wasn't found, so add it
    s_vBreakpoints.push_back( i_rcBreakpoint );
  }
  else
  {
    // Toggle
    iter->m_bEnabled = !iter->m_bEnabled;
  }

  rumDebugInterface::RequestSettingsUpdate();
}


// static
void rumDebugVM::BuildLocalVariables( HSQUIRRELVM i_pVM, int32_t i_StackLevel )
{
#if DEBUG_OUTPUT
  SQInteger iTopBegin{ sq_gettop( i_pVM ) };
#endif

  s_vLocalVariables.clear();

  int32_t iIndex{ 0 };
  const SQChar* strName{ nullptr };
  while( ( strName = sq_getlocal( i_pVM, i_StackLevel, iIndex++ ) ) )
  {
    const SQObjectType eType{ sq_gettype( i_pVM, -1 ) };

    rumDebugVariable cLocalEntry;
    cLocalEntry.m_strName = strName;
    cLocalEntry.m_strType = rumDebugUtility::GetTypeName( eType );
    cLocalEntry.m_strValue = rumDebugUtility::FormatVariable( i_pVM, -1, rumDebugInterface::WantsValuesAsHex() );

    s_vLocalVariables.emplace_back( std::move( cLocalEntry ) );

    sq_poptop( i_pVM );
  }

#if DEBUG_OUTPUT
  SQInteger iTopEnd{ sq_gettop( i_pVM ) };
  assert( iTopBegin == iTopEnd );
#endif
}


// static
void rumDebugVM::BuildVariables( HSQUIRRELVM i_pVM, std::vector<rumDebugVariable>& io_vVariables )
{
  for( auto& watchIter : io_vVariables )
  {
    // Check locals first
    const auto& localIter{ std::find( s_vLocalVariables.begin(), s_vLocalVariables.end(), watchIter ) };
    if( localIter != s_vLocalVariables.end() )
    {
      // This watch variable is a local variable, so just re-use that info
      watchIter.m_strType = localIter->m_strType;
      watchIter.m_strValue = localIter->m_strValue;
    }
    else
    {
      SQObject sqObject{ rumDebugUtility::FindSymbol( i_pVM, watchIter.m_strName, s_uiLocalVariableStackLevel ) };
      if( rumDebugUtility::IsUnknownType( sqObject._type ) )
      {
        // Try one more time with a "this." prefix
        std::string strVariable( "this." + watchIter.m_strName );
        sqObject = rumDebugUtility::FindSymbol( i_pVM, strVariable, s_uiLocalVariableStackLevel );
      }

      watchIter.m_strType = rumDebugUtility::GetTypeName( sqObject._type );
      watchIter.m_strValue = rumDebugUtility::FormatVariable( i_pVM, sqObject, rumDebugInterface::WantsValuesAsHex() );
    }
  }
}


// static
SQInteger rumDebugVM::DetachVM( HSQUIRRELVM i_pVM )
{
  const auto& iter{ std::find( s_vDebugContexts.begin(), s_vDebugContexts.end(), i_pVM ) };
  if( iter != s_vDebugContexts.end() )
  {
    bool bPaused{ iter->m_bPaused };

    s_vDebugContexts.erase( iter );
    sq_setnativedebughook( i_pVM, NULL );

    if( bPaused )
    {
      RequestResume();
    }

    if( s_pCurrentVM == i_pVM )
    {
      s_pCurrentVM = nullptr;
    }

    return SQ_OK;
  }

  return SQ_ERROR;
}


// static
void rumDebugVM::DetachVM( const std::string& i_strName )
{
  HSQUIRRELVM pVM{ GetVMByName( i_strName ) };
  if( pVM )
  {
    DetachVM( pVM );
  }
}


HSQUIRRELVM rumDebugVM::GetVMByName( const std::string& i_strName )
{
  const auto& iterVM{ std::find_if( s_mapRegisteredVMs.begin(), s_mapRegisteredVMs.end(),
                                        [&]( const auto& iter )
    {
      return iter.second.compare( i_strName ) == 0;
    } ) };
  if( iterVM != s_mapRegisteredVMs.end() )
  {
    return iterVM->first;
  }

  return nullptr;
}


// static
void rumDebugVM::EnableDebugInfo( HSQUIRRELVM i_pVM, bool i_bEnable )
{
  sq_enabledebuginfo( i_pVM, i_bEnable ? SQTrue : SQFalse );
}


// static
void rumDebugVM::FileClose( const std::filesystem::path& i_fsFilePath )
{
  std::string strFilePath{ i_fsFilePath.generic_string() };

  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  const auto& iter{ s_mapOpenedFiles.find( strFilePath ) };
  if( iter != s_mapOpenedFiles.end() )
  {
    s_mapOpenedFiles.erase( iter );
  }

  rumDebugInterface::RequestSettingsUpdate();
}


// static
void rumDebugVM::FileOpen( const std::filesystem::path& i_fsFilePath, uint32_t i_uiLine )
{
  std::string strFilePath{ i_fsFilePath.generic_string() };

  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  // Determine if the file is already opened
  const auto& iter{ s_mapOpenedFiles.find( strFilePath ) };
  if( iter == s_mapOpenedFiles.end() )
  {
    // Open and cache the file contents
    rumDebugFile cFile;
    cFile.m_fsFilePath = i_fsFilePath;
    cFile.m_strFilename = i_fsFilePath.filename().generic_string();

    std::ifstream cInfile;
    cInfile.open( cFile.m_fsFilePath.native().c_str() );

    std::ostringstream cBuffer;
    cBuffer << cInfile.rdbuf();
    cFile.m_strData = cBuffer.str();

    cFile.m_vStringOffsets.resize( 10000 );

    size_t szLastOffset{ 0 };
    size_t szCurrentOffset{ 0 };

    bool bInMultilineComment{ false };
    uint32_t iMultilineCommentStart{ 0 };

    uint32_t index{ 0 };

    while( ( szCurrentOffset = cFile.m_strData.find( '\n', szLastOffset ) ) != std::string::npos )
    {
      cFile.m_vStringOffsets[++index] = ++szCurrentOffset;

      std::string_view strLine{ std::string_view( cFile.m_strData ).substr( szLastOffset, szCurrentOffset - szLastOffset ) };

      // Parse multiline comment ranges
      if( !bInMultilineComment && strLine.find( "/*" ) != std::string::npos )
      {
        bInMultilineComment = true;
        iMultilineCommentStart = index;
      }
      else if( bInMultilineComment && strLine.find( "*/" ) != std::string::npos )
      {
        rumDebugFile::MultilineComment cMultilineComment;
        cMultilineComment.m_iStartLine = iMultilineCommentStart;
        cMultilineComment.m_iEndLine = index;

        cFile.m_vMultilineComments.emplace_back( std::move( cMultilineComment ) );

        bInMultilineComment = false;
      }

      szLastOffset = szCurrentOffset;
    }

    // Add the last offset if there isn't a final '\n'
    if( szLastOffset != cFile.m_strData.length() )
    {
      cFile.m_vStringOffsets[index++] = cFile.m_strData.length();
    }

    // Handle files that do not have a closing multiline comment
    if( bInMultilineComment )
    {
      rumDebugFile::MultilineComment cMultilineComment;
      cMultilineComment.m_iStartLine = iMultilineCommentStart;
      cMultilineComment.m_iEndLine = index;

      cFile.m_vMultilineComments.emplace_back( std::move( cMultilineComment ) );
    }

    cFile.m_vStringOffsets.resize( index + 1 );

    s_mapOpenedFiles.insert( std::make_pair( strFilePath, std::move( cFile ) ) );
    rumDebugInterface::RequestSettingsUpdate();
  }

  // Request a switch to the target file and line
  rumDebugInterface::SetFileFocus( i_fsFilePath, i_uiLine );
}


// static
SQInteger rumDebugVM::IsDebuggerAttached( HSQUIRRELVM i_pVM )
{
  const auto& iter{ std::find( s_vDebugContexts.begin(), s_vDebugContexts.end(), i_pVM ) };
  return iter != s_vDebugContexts.end() ? SQTrue : SQFalse;
}


// static
void rumDebugVM::NativeDebugHook( HSQUIRRELVM const i_pVM, const SQInteger i_eHookType, const SQChar* i_strFileName,
                                  const SQInteger i_iLine, const SQChar* const i_strFunctionName )
{
  if( SQ_LINEEXECUTION != i_eHookType )
  {
    return;
  }

  auto iterContext{ std::find( s_vDebugContexts.begin(), s_vDebugContexts.end(), i_pVM ) };
  if( iterContext == s_vDebugContexts.end() )
  {
    // The VM is not attached for debugging
    return;
  }

  s_vCurrentDebugContext = &*iterContext;

  SQStackInfos cStackInfos;
  SQInteger iStackLevel{ 0 };
  while( SQ_SUCCEEDED( sq_stackinfos( i_pVM, iStackLevel, &cStackInfos ) ) )
  {
    ++iStackLevel;
  }

  std::filesystem::path fsFilePath( i_strFileName );
  uint32_t uiLine{ static_cast<uint32_t>( i_iLine ) };

  // Check for breakpoints first, even if there is a step directive because breakpoints override step directives
  rumDebugBreakpoint cBreakpoint( fsFilePath, uiLine );
  const auto& iterBP{ std::find_if( s_vBreakpoints.begin(), s_vBreakpoints.end(),
                                    [&]( const auto& i_rcBreakpoint )
    {
      return i_rcBreakpoint.m_bEnabled && ( i_rcBreakpoint == cBreakpoint );
    } ) };

  if( s_vBreakpoints.end() != iterBP )
  {
#if DEBUG_OUTPUT
    std::cout << "Breakpoint hit (type: " << static_cast<int32_t>( i_eHookType );
    std::cout << ") source: " << i_strFileName;
    std::cout << ") line: " << uiLine;
    std::cout << ") function: " << i_strFunctionName << '\n';
#endif // DEBUG_OUTPUT

    if( iterContext != s_vDebugContexts.end() )
    {
      SuspendVM( i_pVM, *iterContext, uiLine, fsFilePath );
    }
  }

  switch( iterContext->m_eStepDirective )
  {
    case rumDebugContext::StepDirective::StepOver:
    {
      iterContext->m_eStepDirective = rumDebugContext::StepDirective::Resume;
      if( iStackLevel <= (SQInteger)iterContext->m_uiStepDirectiveStackLevel )
      {
        SuspendVM( i_pVM, *iterContext, uiLine, fsFilePath );
      }
      break;
    }

    case rumDebugContext::StepDirective::StepInto:
    {
      iterContext->m_eStepDirective = rumDebugContext::StepDirective::Resume;
      if( iStackLevel >= (SQInteger)iterContext->m_uiStepDirectiveStackLevel )
      {
        SuspendVM( i_pVM, *iterContext, uiLine, fsFilePath );
      }
      break;
    }

    case rumDebugContext::StepDirective::StepOut:
    {
      iterContext->m_eStepDirective = rumDebugContext::StepDirective::Resume;
      if( iStackLevel < (SQInteger)iterContext->m_uiStepDirectiveStackLevel )
      {
        SuspendVM( i_pVM, *iterContext, uiLine, fsFilePath );
      }
      break;
    }
  }
}


// static
void rumDebugVM::RegisterVM( HSQUIRRELVM i_pVM, const std::string& i_strName )
{
  auto iter{ s_mapRegisteredVMs.find( i_pVM ) };
  if( iter == s_mapRegisteredVMs.end() )
  {
    s_mapRegisteredVMs.insert( std::pair( i_pVM, i_strName ) );
  }
  else
  {
    iter->second = i_strName;
  }
}


// static
void rumDebugVM::RequestAttachVM( const std::string& i_strName )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );
  s_strAttachRequest = i_strName;
}


// static
void rumDebugVM::RequestDetachVM( const std::string& i_strName )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );
  s_strDetachRequest = i_strName;
}


// static
void rumDebugVM::RequestChangeStackLevel( uint32_t i_uiStackLevel )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  s_uiLocalVariableStackLevel = i_uiStackLevel;

  if( s_vCurrentDebugContext )
  {
    s_vCurrentDebugContext->m_bUpdateVariables = true;
  }
}


// static
void rumDebugVM::RequestResume()
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  if( s_vCurrentDebugContext )
  {
    s_vCurrentDebugContext->m_eStepDirective = rumDebugContext::StepDirective::Resume;
  }
}


// static
void rumDebugVM::RequestStepInto()
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  if( s_vCurrentDebugContext )
  {
    s_vCurrentDebugContext->m_eStepDirective = rumDebugContext::StepDirective::StepInto;
    s_vCurrentDebugContext->m_uiStepDirectiveStackLevel =
      static_cast<uint32_t>( s_vCurrentDebugContext->m_vCallstack.size() );
  }
}


// static
void rumDebugVM::RequestStepOut()
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  if( s_vCurrentDebugContext )
  {
    s_vCurrentDebugContext->m_eStepDirective = rumDebugContext::StepDirective::StepOut;
    s_vCurrentDebugContext->m_uiStepDirectiveStackLevel =
      static_cast<uint32_t>( s_vCurrentDebugContext->m_vCallstack.size() );
  }
}


// static
void rumDebugVM::RequestStepOver()
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  if( s_vCurrentDebugContext )
  {
    s_vCurrentDebugContext->m_eStepDirective = rumDebugContext::StepDirective::StepOver;
    s_vCurrentDebugContext->m_uiStepDirectiveStackLevel =
      static_cast<uint32_t>( s_vCurrentDebugContext->m_vCallstack.size() );
  }
}


// static
void rumDebugVM::RequestVariable( const rumDebugVariable& i_cVariable )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  s_vRequestedVariables.push_back( i_cVariable );

  if( s_vCurrentDebugContext )
  {
    s_vCurrentDebugContext->m_bUpdateVariables = true;
  }
}


// static
void rumDebugVM::RequestVariableUpdates()
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  if( s_vCurrentDebugContext )
  {
    s_vCurrentDebugContext->m_bUpdateVariables = true;
  }
}


// static
void rumDebugVM::SuspendVM( HSQUIRRELVM i_pVM, rumDebugContext& i_rcContext, uint32_t i_uiLine,
                            const std::filesystem::path& i_fsFilePath )
{
  i_rcContext.m_vCallstack.clear();

  i_rcContext.m_bFocusOnCurrentInstruction = true;

  i_rcContext.m_uiPausedLine = i_uiLine;
  i_rcContext.m_fsPausedFile = i_fsFilePath;

  FileOpen( i_fsFilePath, i_uiLine );

  SQStackInfos cStackInfos;
  SQInteger iStackLevel{ 0 };
  while( SQ_SUCCEEDED( sq_stackinfos( i_pVM, iStackLevel++, &cStackInfos ) ) )
  {
    i_rcContext.m_vCallstack.push_back( { static_cast<int32_t>( cStackInfos.line ),
                                          std::string( cStackInfos.source ),
                                          std::string( cStackInfos.funcname ) } );
  }

  s_pCurrentVM = i_pVM;
  s_uiLocalVariableStackLevel = 0;

  do
  {
    {
#if DEBUG_OUTPUT
      std::cout << "Parsing variables\n";
#endif

      std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

      BuildLocalVariables( i_pVM, s_uiLocalVariableStackLevel );
      BuildVariables( i_pVM, s_vWatchVariables );
      BuildVariables( i_pVM, s_vRequestedVariables );

      i_rcContext.m_bUpdateVariables = false;
    }

    i_rcContext.m_bPaused = true;

    std::unique_lock<std::mutex> ulock( s_mtxDebugLock );
    s_cvDebugLock.wait( ulock );

    i_rcContext.m_bPaused = false;
  } while( i_rcContext.m_bUpdateVariables );

  s_vRequestedVariables.clear();
}


// static
void rumDebugVM::Update()
{
  if( !s_strAttachRequest.empty() )
  {
    std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

    AttachVM( s_strAttachRequest );
    s_strAttachRequest.clear();
  }

  if( !s_strDetachRequest.empty() )
  {
    std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

    DetachVM( s_strDetachRequest );
    s_strDetachRequest.clear();
  }
}


// static
bool rumDebugVM::WatchVariableAdd( const std::string& i_strName )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  auto iter{ std::find( s_vWatchVariables.begin(), s_vWatchVariables.end(), i_strName ) };
  if( s_vWatchVariables.end() == iter )
  {
    rumDebugVariable cVariable;
    cVariable.m_strName = i_strName;
    s_vWatchVariables.emplace_back( std::move( cVariable ) );

    if( s_vCurrentDebugContext )
    {
      s_vCurrentDebugContext->m_bUpdateVariables = true;
    }

    rumDebugInterface::RequestSettingsUpdate();

    return true;
  }

  return false;
}


// static
bool rumDebugVM::WatchVariableEdit( const rumDebugVariable& i_rcVariable, const std::string& i_strName )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  auto iter{ std::find( s_vWatchVariables.begin(), s_vWatchVariables.end(), i_rcVariable ) };
  if( iter != s_vWatchVariables.end() )
  {
    iter->m_strName = i_strName;
    iter->m_strValue.clear();
    iter->m_strType.clear();

    if( s_vCurrentDebugContext )
    {
      s_vCurrentDebugContext->m_bUpdateVariables = true;
    }

    rumDebugInterface::RequestSettingsUpdate();

    return true;
  }

  return false;
}


// static
void rumDebugVM::WatchVariableRemove( const rumDebugVariable& i_rcVariable )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxAccessLock );

  auto iter{ std::find( s_vWatchVariables.begin(), s_vWatchVariables.end(), i_rcVariable ) };
  if( iter != s_vWatchVariables.end() )
  {
    // Remove the variable
    s_vWatchVariables.erase( iter );
    rumDebugInterface::RequestSettingsUpdate();
  }
}

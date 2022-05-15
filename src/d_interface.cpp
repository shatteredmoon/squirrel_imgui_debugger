/*

Squirrel ImGui Debugger Interface

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

#include <d_interface.h>

#include <d_breakpoint.h>
#include <d_settings.h>
#include <d_utility.h>
#include <d_variable.h>
#include <d_vm.h>

#include <regex>

#include <imgui_internal.h>
#include <NetImgui_Api.h>
#include <private/NetImgui_CmdPackets.h>


std::string rumDebugInterface::s_strScriptPath;
std::filesystem::path rumDebugInterface::s_fsFocusFile;
int32_t rumDebugInterface::s_iFocusLine{ -1 };
std::mutex rumDebugInterface::s_mtxLockGuard;
bool rumDebugInterface::s_bUpdateSettings;

ImU32 rumDebugInterface::s_uiEnabledBreakpointColor{ 0 };
ImU32 rumDebugInterface::s_uiDisabledBreakpointColor{ 0 };


// static
void rumDebugInterface::DisplayVariable( const rumDebugVariable& i_rcVariable )
{
  ImGui::TableNextRow();

  // The local variable name
  ImGui::TableNextColumn();
  ImGui::TextUnformatted( i_rcVariable.m_strName.c_str() );

  // The local variable type
  ImGui::TableNextColumn();
  ImGui::TextUnformatted( i_rcVariable.m_strType.c_str() );

  // The local variable value
  ImGui::TableNextColumn();

  // Preview the results if there are more than 3 lines
  const auto szOffset{ FindNthOccurrence( i_rcVariable.m_strValue, "\n", NUM_VARIABLE_PREVIEW_LINES ) };
  if( szOffset == std::string::npos )
  {
    ImGui::TextUnformatted( i_rcVariable.m_strValue.c_str() );
  }
  else
  {
    ImGui::TextUnformatted( i_rcVariable.m_strValue.substr( 0, szOffset ).c_str() );
    DoVariableExpansion( i_rcVariable );
  }
}


// static
void rumDebugInterface::DoVariableExpansion( const rumDebugVariable& i_rcDebugVariable )
{
  std::string strButtonID{ "...##" + i_rcDebugVariable.m_strName };
  if( ImGui::SmallButton( strButtonID.c_str() ) )
  {
    ImGui::OpenPopup( i_rcDebugVariable.m_strName.c_str() );
  }

  ImGui::SetNextWindowSize( ImVec2( 200, 300 ), ImGuiCond_FirstUseEver );
  if( ImGui::BeginPopupModal( i_rcDebugVariable.m_strName.c_str(), nullptr,
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_HorizontalScrollbar ) )
  {
    if( ImGui::SmallButton( "Copy" ) )
    {
      ImGui::SetClipboardText( i_rcDebugVariable.m_strValue.c_str() );
    }

    ImGui::SameLine();
    if( ImGui::SmallButton( "Close" ) )
    {
      ImGui::CloseCurrentPopup();
    }

    ImGui::Text( i_rcDebugVariable.m_strType.c_str() );
    ImGui::Separator();
    ImGui::TextUnformatted( i_rcDebugVariable.m_strValue.c_str() );

    ImGui::EndPopup();
  }
}


// static
size_t rumDebugInterface::FindNthOccurrence( const std::string& i_strSource, const std::string& i_strFind,
                                             size_t i_szOccurence, size_t i_szOffset )
{
  size_t szPos = i_strSource.find( i_strFind, i_szOffset );
  if( ( 0 == i_szOccurence ) || ( std::string::npos == szPos ) )
  {
    return szPos;
  }

  return FindNthOccurrence( i_strSource, i_strFind, i_szOccurence - 1, szPos + 1 );
}


// static
void rumDebugInterface::DisplayCode( const std::string& i_strSource, uint32_t i_uiLine, bool i_bInMultilineComment )
{
  static ImVec4 uiCommentColor{ 0.34f, 0.65f, 0.29f, 1.0f };
  static ImVec4 uiOperatorColor{ 0.8f, 0.8f, 0.0f, 1.0f };
  static ImVec4 uiReservedWordColor{ 0.34f, 0.61f, 0.76f, 1.0f };
  static ImVec4 uiStringColor{ 0.84f, 0.62f, 0.46f, 1.0f };

  bool bInComment{ false };
  bool bInDoubleQuotes{ false };
  bool bInSingleQuotes{ false };
  size_t szOffset{ 0 };

  std::regex rx( R"(/\*|\*/|\\'|\\"|//|[():;=+-, !^&*\[\]\|\\'\"<>?~])" );
  std::sregex_token_iterator iter( i_strSource.begin(), i_strSource.end(), rx, { -1, 0 } );
  std::vector<std::string> vTokens;
  std::remove_copy_if( iter, std::sregex_token_iterator(),
                       std::back_inserter( vTokens ),
                       []( std::string const& s ) { return s.empty(); } );

  auto pcContext{ rumDebugVM::GetCurrentDebugContext() };

  int32_t iIndex{ 0 };
  for( const auto& token : vTokens )
  {
    if( iIndex > 0 )
    {
      ImGui::SameLine(0.0f, 0.0f);
    }

    if( i_bInMultilineComment || ( token.compare( "/*" ) == 0 ) || ( token.compare( "*/" ) == 0 ) )
    {
      if( token.compare( "/*" ) == 0 )
      {
        i_bInMultilineComment = true;
      }
      else if( token.compare( "*/" ) == 0 )
      {
        i_bInMultilineComment = false;
      }

      ImGui::PushStyleColor( ImGuiCol_Text, uiCommentColor );
      ImGui::TextUnformatted( token.c_str() );
      ImGui::PopStyleColor();
    }
    else if( bInComment || ( token.compare( "//" ) == 0 ) )
    {
      bInComment = true;
      ImGui::PushStyleColor( ImGuiCol_Text, uiCommentColor );
      ImGui::TextUnformatted( token.c_str() );
      ImGui::PopStyleColor();
    }
    else if( bInDoubleQuotes || ( token.compare( "\"" ) == 0 ) )
    {
      if( token.compare( "\"" ) == 0 )
      {
        bInDoubleQuotes = !bInDoubleQuotes;
      }

      ImGui::PushStyleColor( ImGuiCol_Text, uiStringColor );
      ImGui::TextUnformatted( token.c_str() );
      ImGui::PopStyleColor();
    }
    else if( bInSingleQuotes || ( token.compare( "'" ) == 0 ) )
    {
      if( token.compare( "'" ) == 0 )
      {
        bInSingleQuotes = !bInSingleQuotes;
      }

      ImGui::PushStyleColor( ImGuiCol_Text, uiStringColor );
      ImGui::TextUnformatted( token.c_str() );
      ImGui::PopStyleColor();
    }
    else if( rumDebugUtility::IsReservedWord( token ) )
    {
      ImGui::PushStyleColor( ImGuiCol_Text, uiReservedWordColor );
      ImGui::TextUnformatted( token.c_str() );
      ImGui::PopStyleColor();
    }
    else if( rumDebugUtility::IsOperator( token ) )
    {
      ImGui::PushStyleColor( ImGuiCol_Text, uiOperatorColor );
      ImGui::TextUnformatted( token.c_str() );
      ImGui::PopStyleColor();
    }
    else
    {
      ImGui::BeginGroup();
      ImGui::TextUnformatted( token.c_str() );

      if( pcContext->m_bPaused )
      {
        if( ImGui::IsItemHovered() )
        {
          // Only show tooltip info for potential variable names
          std::regex rx( R"([a-zA-Z_]+)" );
          if( std::regex_search( token, rx ) )
          {
            rumDebugVariable cVariable{ GetVariable( token ) };
            ImGui::BeginTooltip();
            static constexpr ImGuiTableFlags eTableFlags{ ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Borders |
                                                          ImGuiTableFlags_NoSavedSettings };
            if( ImGui::BeginTable( "LocalsTable", 3, eTableFlags ) )
            {
              DisplayVariable( cVariable );
              ImGui::EndTable();
            }
            ImGui::EndTooltip();
          }
        }

        // Build a unique ID for the token base on the line and index
        std::string strID( token + "_" );
        strID += std::to_string( i_uiLine );
        strID += "_";
        strID += std::to_string( iIndex );

        if( ImGui::BeginPopupContextItem( strID.c_str() ) )
        {
          if( ImGui::SmallButton( "Copy" ) )
          {
            ImGui::SetClipboardText( token.c_str() );
            ImGui::CloseCurrentPopup();
          }
          else if( ImGui::SmallButton( "Watch" ) )
          {
            if( rumDebugVM::WatchVariableAdd( token ) )
            {
              rumDebugVM::s_cvDebugLock.notify_all();
            }

            ImGui::CloseCurrentPopup();
          }

          ImGui::EndPopup();
        }
      }

      ImGui::EndGroup();
    }

    ++iIndex;
  }
}


rumDebugVariable rumDebugInterface::GetVariable( const std::string& i_strName )
{
  rumDebugVariable cVariable;

  // Check local variables
  const auto& rcvLocalVariables{ rumDebugVM::GetLocalVariablesRef() };
  const auto& localIter{ std::find( rcvLocalVariables.begin(), rcvLocalVariables.end(), i_strName ) };
  if( localIter != rcvLocalVariables.end() )
  {
    cVariable = *localIter;
  }
  else
  {
    // Check watched variables
    const auto& rcvWatchedVariables{ rumDebugVM::GetWatchedVariablesRef() };
    const auto& watchIter{ std::find( rcvWatchedVariables.begin(), rcvWatchedVariables.end(), i_strName ) };
    if( watchIter != rcvWatchedVariables.end() )
    {
      cVariable = *watchIter;
    }
    else
    {
      // Check the recently requested variables
      const auto& rcvRequestedVariables{ rumDebugVM::GetRequestedVariablesRef() };
      const auto& watchIter{ std::find( rcvRequestedVariables.begin(), rcvRequestedVariables.end(), i_strName ) };
      if( watchIter != rcvRequestedVariables.end() )
      {
        cVariable = *watchIter;
      }
      else
      {
        cVariable.m_strName = i_strName;
        rumDebugVM::RequestVariable( cVariable );
        rumDebugVM::s_cvDebugLock.notify_all();
      }
    }
  }

  return cVariable;
}


// static
void rumDebugInterface::Init( const std::string& i_strName, uint32_t i_iPort, const std::string& i_strScriptPath )
{
  using namespace NetImgui::Internal;

  auto cContext{ ImGui::CreateContext() };
  ImGuiIO& io = ImGui::GetIO();

  ImGuiSettingsHandler ini_handler;
  ini_handler.TypeName = "UserData";
  ini_handler.TypeHash = ImHashStr( "UserData" );
  ini_handler.ReadOpenFn = Settings_ReadOpen; // Called when entering into a new ini entry e.g. "[Window][Name]"
  ini_handler.ReadLineFn = Settings_ReadLine; // Called for every line of text within an ini entry
  ini_handler.WriteAllFn = Settings_WriteAll; // Output every entries into 'out_buf'
  cContext->SettingsHandlers.push_back( ini_handler );

  // Add support for function keys
  io.KeyMap[ImGuiKey_G] = static_cast<int>( CmdInput::eVirtualKeys::vkKeyboardA ) + ( ImGuiKey_G - ImGuiKey_A );
  io.KeyMap[ImGuiKey_F5] = static_cast<int>( CmdInput::eVirtualKeys::vkKeyboardSuperF1 ) + ( ImGuiKey_F5 - ImGuiKey_F1 );
  io.KeyMap[ImGuiKey_F9] = static_cast<int>( CmdInput::eVirtualKeys::vkKeyboardSuperF1 ) + ( ImGuiKey_F9 - ImGuiKey_F1 );
  io.KeyMap[ImGuiKey_F10] = static_cast<int>( CmdInput::eVirtualKeys::vkKeyboardSuperF1 ) + ( ImGuiKey_F10 - ImGuiKey_F1 );
  io.KeyMap[ImGuiKey_F11] = static_cast<int>( CmdInput::eVirtualKeys::vkKeyboardSuperF1 ) + ( ImGuiKey_F11 - ImGuiKey_F1 );

  io.DisplaySize.x = static_cast<float>( DEBUGGER_DISPLAY_WIDTH );
  io.DisplaySize.y = static_cast<float>( DEBUGGER_DISPLAY_HEIGHT );

  int32_t iWidth, iHeight;
  unsigned char* pPixels{ nullptr };
  io.Fonts->GetTexDataAsRGBA32( &pPixels, &iWidth, &iHeight );

  NetImgui::Startup();
  NetImgui::ConnectFromApp( i_strName.c_str(), i_iPort );

  s_strScriptPath = i_strScriptPath;

  s_uiEnabledBreakpointColor = ImGui::GetColorU32( ImVec4( 0.4f, 0.0f, 0.0f, 1.0f ) );
  s_uiDisabledBreakpointColor = ImGui::GetColorU32( ImVec4( 0.4f, 0.4f, 0.0f, 1.0f ) );
}


// static
void rumDebugInterface::RequestSettingsUpdate()
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxLockGuard );
  s_bUpdateSettings = true;
}


// static
void rumDebugInterface::SetFileFocus( const std::filesystem::path& i_fsFocusFile, int32_t i_iFocusLine )
{
  std::lock_guard<std::mutex> cLockGuard( s_mtxLockGuard );
  s_fsFocusFile = i_fsFocusFile;
  s_iFocusLine = i_iFocusLine;
}


// static
void rumDebugInterface::Settings_ReadLine( ImGuiContext* i_pContext, ImGuiSettingsHandler* i_pSettingsHandler, void* i_pEntry,
                                           const char* i_strLine )
{
  std::string strLine( i_strLine );
  if( strLine.find_first_of( "Breakpoint" ) == 0 )
  {
    size_t szLineNumberStart{ strLine.find_first_of( '=' ) + 1 };
    size_t szBreakpointEnabled{ strLine.find_first_of( ',' ) + 1 };
    size_t szFilePathStart{ strLine.find( ',', szBreakpointEnabled ) + 1 };

    if( szLineNumberStart != std::string::npos && szFilePathStart != std::string::npos )
    {
      std::string strLineNumber{ strLine.substr( szLineNumberStart, szBreakpointEnabled - szLineNumberStart - 1 ) };
      std::string strBreakpointEnabled{ strLine.substr( szBreakpointEnabled, 1 ) };
      std::filesystem::path fsFilePath{ strLine.substr( szFilePathStart ) };

      rumDebugBreakpoint cBreakpoint( fsFilePath, std::stoi( strLineNumber ),
                                      strBreakpointEnabled.compare( "1" ) == 0 ? true : false );
      rumDebugVM::BreakpointAdd( cBreakpoint );
    }
  }
  else if( strLine.find_first_of( "File" ) == 0 )
  {
    std::filesystem::path fsFilePath{ strLine.substr( strLine.find_first_of( "=" ) + 1 ) };
    rumDebugVM::FileOpen( fsFilePath, 0 );
  }
  else if( strLine.find_first_of( "WatchVariable" ) == 0 )
  {
    rumDebugVM::WatchVariableAdd( strLine.substr( strLine.find_first_of( "=" ) + 1 ) );
  }
}


// static
void* rumDebugInterface::Settings_ReadOpen( ImGuiContext* i_pContext, ImGuiSettingsHandler* i_pSettingsHandler,
                                            const char* i_strName )
{
  ImGuiWindowSettings* pcSettings{ ImGui::FindOrCreateWindowSettings( i_strName ) };
  ImGuiID id{ pcSettings->ID };

  // Clear existing if recycling previous entry
  *pcSettings = ImGuiWindowSettings();
  pcSettings->ID = id;
  pcSettings->WantApply = true;

  return (void*)pcSettings;
}


// static
void rumDebugInterface::Settings_WriteAll( ImGuiContext* i_pContext, ImGuiSettingsHandler* i_pSettingsHandler,
                                           ImGuiTextBuffer* io_pBuffer )
{
  // Ballpark reserve
  io_pBuffer->reserve( io_pBuffer->size() + 1000 );
  io_pBuffer->appendf( "[UserData][Script Debugger]\n" );

  uint32_t uiBreakpointIndex{ 1 };
  for( const auto& iter : rumDebugVM::GetBreakpointsRef() )
  {
    io_pBuffer->appendf( "Breakpoint%d=%d,%d,%s\n",
                         uiBreakpointIndex++, iter.m_uiLine, iter.m_bEnabled ? 1 : 0,
                         iter.m_fsFilepath.generic_string().c_str() );
#if DEBUG_OUTPUT
    std::cout << "Saving Breakpoint: " << iter.m_uiLine << '\n';
#endif
  }

  uint32_t uiFileIndex{ 1 };
  for( const auto& iter : rumDebugVM::GetOpenedFilesRef() )
  {
    io_pBuffer->appendf( "File%d=%s\n", uiFileIndex++, iter.second.m_fsFilePath.generic_string().c_str() );
#if DEBUG_OUTPUT
    std::cout << "Saving File: " << iter.second.m_fsFilePath.generic_string() << '\n';
#endif
  }

  uint32_t uiWatchVariableIndex{ 1 };
  for( const auto& iter : rumDebugVM::GetWatchedVariablesRef() )
  {
    io_pBuffer->appendf( "WatchVariable%d=%s\n", uiWatchVariableIndex++, iter.m_strName.c_str() );
#if DEBUG_OUTPUT
    std::cout << "Saving WatchVariable: " << iter.m_strName << '\n';
#endif
  }
}


// static
void rumDebugInterface::Shutdown()
{
  NetImgui::Shutdown();
  ImGui::DestroyContext();
}


// static
void rumDebugInterface::Update()
{
  ImGuiIO& rcIO{ ImGui::GetIO() };

  // TODO - pass in the actual amount
  rcIO.DeltaTime = 1.0f / 60.0f; // set the time elapsed since the previous frame (in seconds)

#if DEBUG_OUTPUT
  for( ImGuiKey key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_COUNT; key++ )
  {
    if( ImGui::IsKeyReleased( key ) )
    {
      std::cout << "Key down: " << ImGui::GetKeyName( key ) << '\n';
    }
  }
#endif // DEBUG_OUTPUT

  UpdateKeyDirectives();

  NetImgui::NewFrame();

  static bool bOpen{ true };
  if( ImGui::Begin( "Script Debugger", &bOpen, ImGuiWindowFlags_NoScrollbar ) )
  {
    // A table split by variable inspection, breakpoints, and callstack
    if( ImGui::BeginTable( "MainTable", 1, ImGuiTableFlags_None, ImVec2( CODE_DISPLAY_WIDTH, CODE_DISPLAY_HEIGHT ) ) )
    {
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      UpdatePrimaryRow();

      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      UpdateSecondaryRow();

      // MainTable
      ImGui::EndTable();
    }
  }

  // Script Debugger window
  ImGui::End();

  //ImGui::ShowDemoWindow();

  NetImgui::EndFrame();

  if( s_bUpdateSettings )
  {
    UpdateSettings();
  }
}


// static
void rumDebugInterface::UpdateBreakpointTab()
{
  if( ImGui::BeginTabItem( "Breakpoints##TabItem" ) )
  {
    const ImVec2 vRegion{ ImGui::GetContentRegionAvail() };
    const float fSizeY{ std::max( 0.0f, vRegion.y - 2.0f ) };
    ImGui::BeginChild( "BreakpointsTabChild", ImVec2( 0.f, fSizeY ), false, ImGuiWindowFlags_HorizontalScrollbar );

    // Fetch by copy here because there is potential to modify the list during iteration
    const auto cvBreakpoints{ rumDebugVM::GetBreakpointsCopy() };
    if( cvBreakpoints.empty() )
    {
      ImGui::TextUnformatted( "No breakpoints set" );
    }
    else
    {
      static constexpr ImGuiTableFlags eTableFlags{ ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                                                    ImGuiTableFlags_Borders | ImGuiTableFlags_NoSavedSettings };
      if( ImGui::BeginTable( "BreakpointsTable", 3, eTableFlags ) )
      {
        rumDebugBreakpoint cRemovedBreakpoint;
        bool bBreakpointRemoved{ false };

        for( const auto& iter : cvBreakpoints )
        {
          ImGui::TableNextRow();

          // The breakpoint enabled/disabled status
          ImGui::TableNextColumn();
          ImGui::TableSetBgColor( ImGuiTableBgTarget_CellBg,
                                  iter.m_bEnabled ? s_uiEnabledBreakpointColor : s_uiDisabledBreakpointColor );
          ImGui::TextUnformatted( " * " );
          if( ImGui::IsItemHovered() )
          {
            if( ImGui::IsKeyPressed( ImGuiKey_F9 ) || ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            {
              rumDebugVM::BreakpointToggle( iter );
            }
            else if( ImGui::IsKeyPressed( ImGuiKey_Delete ) )
            {
              // Schedule for removal since we're mid-iteration
              cRemovedBreakpoint = iter;
              bBreakpointRemoved = true;
            }
          }

          // The breakpoint line number
          ImGui::TableNextColumn();
          ImGui::Text( "Line %d", iter.m_uiLine );
          if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
          {
            rumDebugVM::FileOpen( iter.m_fsFilepath, iter.m_uiLine );
          }

          // The breakpoint source
          ImGui::TableNextColumn();
          ImGui::TextUnformatted( iter.m_fsFilepath.filename().generic_string().c_str() );
          if( ImGui::IsItemHovered() )
          {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted( iter.m_fsFilepath.generic_string().c_str() );
            ImGui::EndTooltip();

            if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            {
              rumDebugVM::FileOpen( iter.m_fsFilepath, iter.m_uiLine );
            }
          }
        }

        if( bBreakpointRemoved )
        {
          rumDebugVM::BreakpointRemove( cRemovedBreakpoint );
        }

        // BreakpointsTable
        ImGui::EndTable();
      }
    }

    // BreakpointsTabChild
    ImGui::EndChild();

    // BreakpointsTabItem
    ImGui::EndTabItem();
  }
}


// static
void rumDebugInterface::UpdateDisplayFolder( const std::string& i_strFolder,
                                             std::filesystem::recursive_directory_iterator& i_rcIter,
                                             std::filesystem::recursive_directory_iterator& i_rcIterEnd,
                                             const std::string& i_strFilter )
{
  while( i_rcIter != i_rcIterEnd )
  {
    auto strPath{ i_rcIter->path().generic_string() };

    auto strChildPath{ strPath };
    if( strChildPath.find( i_strFolder ) == std::string::npos )
    {
      break;
    }

    if( std::filesystem::is_directory( i_rcIter->path() ) )
    {

      if( ImGui::TreeNode( i_rcIter->path().stem().generic_string().c_str() ) )
      {
        // The tree node is expanded, so show its contents
        UpdateDisplayFolder( strPath, ++i_rcIter, i_rcIterEnd, i_strFilter );
        ImGui::TreePop();
      }
      else
      {
        // The tree node is collapsed, so skip its contents
        UpdateSkipChildren( strPath, i_rcIter, i_rcIterEnd );
      }
    }
    else
    {
      std::string strFilename{ i_rcIter->path().filename().generic_string() };

      if( i_strFilter.empty() || strFilename.find( i_strFilter ) != std::string::npos )
      {
        if( ImGui::Selectable( strFilename.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick ) )
        {
          rumDebugVM::FileOpen( i_rcIter->path(), 0 );
        }
      }

      ++i_rcIter;
    }
  }
}


// static
void rumDebugInterface::UpdateFileExplorer()
{
  static char strCFilter[MAX_FILENAME_LENGTH];

  // Adds a text input field for filtering file and folder names
  ImGui::InputText( "##FileFilter", strCFilter, IM_ARRAYSIZE( strCFilter ) );

  // Adds a clear button for clearing the file filter
  ImGui::SameLine();
  if( ImGui::Button( "Clear" ) )
  {
    memset( strCFilter, '\0', sizeof( char ) * MAX_FILENAME_LENGTH );
  }

  const ImVec2 vRegion{ ImGui::GetContentRegionAvail() };
  const float fSizeY{ std::max( 0.0f, vRegion.y - 2.0f ) };
  ImGui::BeginChild( "FileExplorerTabChild", ImVec2( 0.f, fSizeY ), false, ImGuiWindowFlags_HorizontalScrollbar );

  std::string strFilter( strCFilter );

  std::filesystem::path fsPath( s_strScriptPath );
  std::filesystem::recursive_directory_iterator end, dir( fsPath );
  while( dir != end )
  {
    UpdateDisplayFolder( dir->path().generic_string(), dir, end, strFilter );
  }

  // FileExplorerTabChild
  ImGui::EndChild();
}


// static
void rumDebugInterface::UpdateLocalsTab()
{
  auto pcContext{ rumDebugVM::GetCurrentDebugContext() };
  if( !pcContext )
  {
    return;
  }

  if( ImGui::BeginTabItem( "Locals##TabItem" ) )
  {
    const ImVec2 vRegion{ ImGui::GetContentRegionAvail() };
    const float fSizeY{ std::max( 0.0f, vRegion.y - 2.0f ) };
    ImGui::BeginChild( "LocalsTabChild", ImVec2( 0.f, fSizeY ), false, ImGuiWindowFlags_HorizontalScrollbar );

    if( pcContext->m_bPaused )
    {
      static constexpr ImGuiTableFlags eTableFlags{ ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                                                    ImGuiTableFlags_Borders | ImGuiTableFlags_NoSavedSettings };
      if( ImGui::BeginTable( "LocalsTable", 3, eTableFlags ) )
      {
        const auto& rcvLocalVariables{ rumDebugVM::GetLocalVariablesRef() };
        for( const auto& iter : rcvLocalVariables )
        {
          DisplayVariable( iter );
        }

        // LocalsTable
        ImGui::EndTable();
      }
    }
    else
    {
      ImGui::Text( "Running" );
    }

    // LocalsTabChild
    ImGui::EndChild();

    // LocalsTabItem
    ImGui::EndTabItem();
  }
}


// static
void rumDebugInterface::UpdateKeyDirectives()
{
  auto pcContext{ rumDebugVM::GetCurrentDebugContext() };
  if( !pcContext || !pcContext->m_bPaused )
  {
    return;
  }

  ImGuiIO& rcIO{ ImGui::GetIO() };

  if( ImGui::IsKeyPressed( ImGuiKey_F5 ) )
  {
#if DEBUG_OUTPUT
    std::cout << "Resuming\n";
#endif
    rumDebugVM::RequestResume();
    rumDebugVM::s_cvDebugLock.notify_all();
  }
  else if( ImGui::IsKeyPressed( ImGuiKey_F10 ) )
  {
#if DEBUG_OUTPUT
    std::cout << "Step over\n";
#endif
    rumDebugVM::RequestStepOver();
    rumDebugVM::s_cvDebugLock.notify_all();
  }
  else if( ImGui::IsKeyPressed( ImGuiKey_F11 ) )
  {
    if( rcIO.KeyShift )
    {
#if DEBUG_OUTPUT
      std::cout << "Step out\n";
#endif
      rumDebugVM::RequestStepOut();
    }
    else
    {
#if DEBUG_OUTPUT
      std::cout << "Step into\n";
#endif
      rumDebugVM::RequestStepInto();
    }

    rumDebugVM::s_cvDebugLock.notify_all();
  }
}


// static
void rumDebugInterface::UpdatePrimaryRow()
{
  // A table split by a file explorer on the left and source code on the right
  ImGuiTableFlags eTableFlags{ ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY };
  if( ImGui::BeginTable( "PrimaryRow", 2, eTableFlags, ImVec2( 0.f, 568.f ) ) )
  {
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    UpdateFileExplorer();

    ImGui::TableNextColumn();
    UpdateSourceCode();

    // PrimaryRow
    ImGui::EndTable();
  }
}


// static
void rumDebugInterface::UpdateSecondaryRow()
{
  static ImGuiTableFlags eTableFlags{ ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY };
  if( ImGui::BeginTable( "SecondaryRow", 2, eTableFlags ) )
  {
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    UpdateWatchLocalWindow();

    ImGui::TableNextColumn();
    UpdateStackBreakpointWindow();

    // SecondaryRow
    ImGui::EndTable();
  }
}


// static
void rumDebugInterface::UpdateSettings()
{
  ImGuiContext& rcContext{ *GImGui };
  if( rcContext.SettingsLoaded )
  {
    ImGui::SaveIniSettingsToDisk( rcContext.IO.IniFilename );

    std::lock_guard<std::mutex> cLockGuard( s_mtxLockGuard );
    s_bUpdateSettings = false;
  }
}


// static
void rumDebugInterface::UpdateSkipChildren( const std::string& i_strFolder,
                                            std::filesystem::recursive_directory_iterator& i_rcIter,
                                            std::filesystem::recursive_directory_iterator& i_rcIterEnd )
{
  while( ++i_rcIter != i_rcIterEnd )
  {
    auto strChildPath{ i_rcIter->path().generic_string() };
    if( strChildPath.find( i_strFolder ) == std::string::npos )
    {
      break;
    }
  }
}


// static
void rumDebugInterface::UpdateSourceCode()
{
  static ImU32 uiCurrentLineColor{ ImGui::GetColorU32( ImVec4( 0.2f, 0.4f, 0.7f, 0.5f ) ) };

  ImGuiIO& rcIO{ ImGui::GetIO() };

  if( ImGui::BeginTabBar( "OpenedFiles", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll ) )
  {
    // Fetch by copy here because there is potential to modify the list during iteration
    // #TODO - There is a lot of data being copied - work out a threadsafe share
    const auto cvOpenedFiles{ rumDebugVM::GetOpenedFilesCopy() };
    for( const auto& iter : cvOpenedFiles )
    {
      const rumDebugFile& rcFile{ iter.second };

      bool bSetFocus{ false };
      if( !s_fsFocusFile.empty() && ( rcFile.m_fsFilePath.compare( s_fsFocusFile ) == 0 ) )
      {
        bSetFocus = true;
      }

      bool bTabOpened{ true };
      if( ImGui::BeginTabItem( rcFile.m_strFilename.c_str(), &bTabOpened,
                               bSetFocus ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None ) )
      {
        const ImVec2 vRegion{ ImGui::GetContentRegionAvail() };
        const float fSizeY{ std::max( 0.0f, vRegion.y - 2.0f ) };
        ImGui::BeginChild( "SourceCodeTabChild", ImVec2( 0.f, fSizeY ), false, ImGuiWindowFlags_HorizontalScrollbar );

        if( bSetFocus )
        {
          s_fsFocusFile.clear();
        }

        std::map<uint32_t, bool> vLinesWithBreakpoints;

        // Parse the breakpoints for matching lines
        const auto& rcvBreakpoints{ rumDebugVM::GetBreakpointsRef() };
        for( const auto& iter : rcvBreakpoints )
        {
          if( iter.m_fsFilepath == rcFile.m_fsFilePath )
          {
            vLinesWithBreakpoints.insert( std::pair( iter.m_uiLine, iter.m_bEnabled ) );
          }
        }

        static constexpr ImGuiTableFlags eTableFlags{ ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | 
                                                      ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                                      ImGuiTableFlags_NoSavedSettings };
        if( ImGui::BeginTable( "SourceCode", 2, eTableFlags ) )
        {
          ImGui::TableSetupColumn( "Line", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3 );
          ImGui::TableSetupColumn( "Source", ImGuiTableColumnFlags_WidthStretch, 0.0f );

          size_t iNumLines{ rcFile.m_vStringOffsets.size() };

          if( rcIO.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_G ) )
          {
            ImGui::OpenPopup( "Go To Line" );
          }

          if( ImGui::BeginPopupModal( "Go To Line", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
          {
            ImGui::Text( "Line number (1 - %d)", static_cast<int32_t>( iNumLines ) );

            static int32_t iGotoLine{ 1 };
            ImGui::InputInt( "##GotoLine", &iGotoLine );

            if( ImGui::Button( "OK", ImVec2( 120, 0 ) ) )
            {
              // Clamp the input values to the valid range
              if( iGotoLine < 1 )
              {
                iGotoLine = 1;
              }
              else if( iGotoLine > static_cast<int32_t>( iNumLines ) )
              {
                iGotoLine = static_cast<int32_t>( iNumLines );
              }

              SetFileFocus( rcFile.m_fsFilePath, iGotoLine );
              ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if( ImGui::Button( "Cancel", ImVec2( 120, 0 ) ) )
            {
              ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
          }

          // We need to adjust the scroll offset, but that can't be done when clipping. Take a frame here to fix the
          // offset, rendering as little as possible to get the job done.
          if( bSetFocus && s_iFocusLine >= 0 )
          {
            for( int32_t iIndex{ 1 }; iIndex <= (int32_t)iNumLines; ++iIndex )
            {
              ImGui::TableNextRow();

              ImGui::TableNextColumn();
              ImGui::Text( "" );

              if( iIndex == s_iFocusLine )
              {
                ImGui::SetScrollHereY();
                s_iFocusLine = -1;
              }
            }
          }
          else
          {
            // Show the table using a clipper since file sizes can be very large
            ImGuiListClipper clipper;
            clipper.Begin( (int32_t)iNumLines );

            // Skip the header row
            while( clipper.Step() )
            {
              for( int32_t iRow{ clipper.DisplayStart }; iRow < clipper.DisplayEnd; ++iRow )
              {
                bool bInMultilineComment{ false };
                int32_t iLine{ iRow + 1 };

                // Determine if this line is inside of a multiline comment
                for( const auto& commentIter : rcFile.m_vMultilineComments )
                {
                  if( iLine > commentIter.m_iStartLine && iLine < commentIter.m_iEndLine )
                  {
                    bInMultilineComment = true;
                    break;
                  }
                }

                ImGui::TableNextRow();

                // The line number column
                ImGui::TableNextColumn();

                const auto& iter{ vLinesWithBreakpoints.find( iLine ) };
                if( iter != vLinesWithBreakpoints.end() )
                {
                  ImGui::TableSetBgColor( ImGuiTableBgTarget_CellBg,
                                          iter->second ? s_uiEnabledBreakpointColor : s_uiDisabledBreakpointColor );
                }

                ImGui::Text( "%d", iLine );

                if( ImGui::IsItemHovered() )
                {
                  rumDebugBreakpoint cBreakpoint( rcFile.m_fsFilePath, iLine );

                  if( ImGui::IsKeyPressed( ImGuiKey_F9 ) || ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                  {
                    rumDebugVM::BreakpointToggle( cBreakpoint );
                  }
                  else if( ImGui::IsKeyPressed( ImGuiKey_Delete ) )
                  {
                    rumDebugVM::BreakpointRemove( cBreakpoint );
                  }
                }

                // The source code column
                ImGui::TableNextColumn();

                if( ( iRow + 1 ) < (int32_t)rcFile.m_vStringOffsets.size() )
                {
                  size_t iBegin{ rcFile.m_vStringOffsets[iRow] };
                  size_t iEnd{ rcFile.m_vStringOffsets[iRow + 1] - 1 };
                  std::string strLine{ rcFile.m_strData.substr( iBegin, iEnd - iBegin ) };

                  const auto pcContext{ rumDebugVM::GetCurrentDebugContext() };
                  if (pcContext && pcContext->m_bPaused && ( pcContext->m_uiPausedLine == iLine ) &&
                      ( pcContext->m_fsPausedFile.compare( rcFile.m_fsFilePath ) == 0 ) )
                  {
                    ImGui::TableSetBgColor( ImGuiTableBgTarget_CellBg, uiCurrentLineColor );
                  }

                  ImGui::BeginGroup();
                  DisplayCode( strLine, iLine, bInMultilineComment );
                  ImGui::EndGroup();

                  if( ImGui::IsItemHovered() && ImGui::IsKeyPressed( ImGuiKey_F9 ) )
                  {
                    rumDebugBreakpoint cBreakpoint{ rcFile.m_fsFilePath, static_cast<uint32_t>( iLine ) };
                    rumDebugVM::BreakpointToggle( cBreakpoint );
                  }
                }
              }
            }

            clipper.End();
          }

          // SourceCode
          ImGui::EndTable();
        }

        // BreakpointsTabChild
        ImGui::EndChild();

        // Filename
        ImGui::EndTabItem();
      }

      if( !bTabOpened )
      {
        rumDebugVM::FileClose( rcFile.m_fsFilePath );
      }
    }

    // OpenedFiles
    ImGui::EndTabBar();
  }
}


// static
void rumDebugInterface::UpdateStackBreakpointWindow()
{
  const ImVec2 vRegion{ ImGui::GetContentRegionAvail() };
  const float fSizeY{ std::max( 0.0f, vRegion.y - 2.0f ) };
  ImGui::BeginChild( "StackAndBreakpointsChild", ImVec2( 0.f, fSizeY ), false, ImGuiWindowFlags_HorizontalScrollbar );

  if( ImGui::BeginTabBar( "StackAndBreakpointsChildTabBar", ImGuiTabBarFlags_None ) )
  {
    UpdateStackTab();
    UpdateBreakpointTab();

    // StackAndBreakpointsTabBar
    ImGui::EndTabBar();
  }

  // StackAndBreakpointsChild
  ImGui::EndChild();
}


// static
void rumDebugInterface::UpdateStackTab()
{
  if( ImGui::BeginTabItem( "Callstack##TabItem" ) )
  {
    const ImVec2 vRegion{ ImGui::GetContentRegionAvail() };
    const float fSizeY{ std::max( 0.0f, vRegion.y - 2.0f ) };
    ImGui::BeginChild( "CallstackTabChild", ImVec2( 0.f, fSizeY ), false, ImGuiWindowFlags_HorizontalScrollbar );

    auto pcContext{ rumDebugVM::GetCurrentDebugContext() };
    if( pcContext )
    {
      if( !pcContext->m_bPaused )
      {
        ImGui::TextUnformatted( "Running" );
      }
      else
      {
        static constexpr ImGuiTableFlags eTableFlags{ ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                                                      ImGuiTableFlags_Borders | ImGuiTableFlags_NoSavedSettings };
        if( ImGui::BeginTable( "CallstackTable", 3, eTableFlags ) )
        {
          uint32_t uiStackIndex{ 0 };
          for( const auto& iterCallstack : pcContext->m_vCallstack )
          {
            ImGui::TableNextRow();

            // The stack line number
            ImGui::TableNextColumn();
            ImGui::Text( "Line %d", iterCallstack.m_iLine );
            if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            {
              rumDebugVM::FileOpen( iterCallstack.m_strFilename, iterCallstack.m_iLine );
              rumDebugVM::RequestChangeStackLevel( uiStackIndex );
              rumDebugVM::s_cvDebugLock.notify_all();
            }

            // The stack source file
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( iterCallstack.m_strFilename.c_str() );
            if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            {
              rumDebugVM::FileOpen( iterCallstack.m_strFilename, iterCallstack.m_iLine );
              rumDebugVM::RequestChangeStackLevel( uiStackIndex );
              rumDebugVM::s_cvDebugLock.notify_all();
            }

            // The function
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( iterCallstack.m_strFunction.c_str() );
            if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            {
              rumDebugVM::FileOpen( iterCallstack.m_strFilename, iterCallstack.m_iLine );
              rumDebugVM::RequestChangeStackLevel( uiStackIndex );
              rumDebugVM::s_cvDebugLock.notify_all();
            }

            ++uiStackIndex;
          }

          // CallstackTable
          ImGui::EndTable();
        }
      }
    }

    // CallstackTabChild
    ImGui::EndChild();

    // Callstack##TabItem
    ImGui::EndTabItem();
  }
}


// static
void rumDebugInterface::UpdateWatchLocalWindow()
{
  const ImVec2 vRegion{ ImGui::GetContentRegionAvail() };
  const float fSizeY{ std::max( 0.0f, vRegion.y - 2.0f ) };
  ImGui::BeginChild( "WatchAndLocalsChild", ImVec2( 0.f, fSizeY ), false, ImGuiWindowFlags_HorizontalScrollbar );

  if( ImGui::BeginTabBar( "WatchAndLocalsChildTabBar", ImGuiTabBarFlags_None ) )
  {
    UpdateWatchTab();
    UpdateLocalsTab();
  
    // WatchAndLocalsChildTabBar
    ImGui::EndTabBar();
  }

  // WatchAndLocalsChild
  ImGui::EndChild();
}


// static
void rumDebugInterface::UpdateWatchTab()
{
  if( ImGui::BeginTabItem( "Watched##TabItem" ) )
  {
    const ImVec2 vRegion{ ImGui::GetContentRegionAvail() };
    const float fSizeY{ std::max( 0.0f, vRegion.y - 2.0f ) };
    ImGui::BeginChild( "WatchedTabChild", ImVec2( 0.f, fSizeY ), false, ImGuiWindowFlags_HorizontalScrollbar );

    static constexpr ImGuiTableFlags eTableFlags{ ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders |
                                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_ContextMenuInBody |
                                                  ImGuiTableFlags_NoSavedSettings };
    if( ImGui::BeginTable( "WatchTable", 3, eTableFlags ) )
    {
      static char strCWatchVariable[MAX_FILENAME_LENGTH];

      // Fetch by copy here because there is potential to modify the list during iteration
      // #TODO - There is a lot of data being copied - work out a threadsafe share
      const auto cvWatchedVariables{ rumDebugVM::GetWatchedVariablesCopy() };
      for( const auto& iter : cvWatchedVariables )
      {
        ImGui::TableNextRow();

        // Watch variable name
        ImGui::TableNextColumn();

        ImGui::PushItemWidth( ImGui::GetColumnWidth() );

        strncpy_s( strCWatchVariable, iter.m_strName.c_str(), iter.m_strName.length() + 1 );
        std::string strContentID{ "##" + iter.m_strName };
        if( ImGui::InputText( strContentID.c_str(), strCWatchVariable, IM_ARRAYSIZE( strCWatchVariable ),
                              ImGuiInputTextFlags_EnterReturnsTrue ) )
        {
          if( rumDebugVM::WatchVariableEdit( iter, strCWatchVariable ) )
          {
            rumDebugVM::s_cvDebugLock.notify_all();
          }
        }

        ImGui::PopItemWidth();

        ImGui::PushID( strContentID.c_str() );
        if( ImGui::BeginPopupContextItem() )
        {
          if( ImGui::Button( "Delete" ) )
          {
            rumDebugVM::WatchVariableRemove( iter );
            ImGui::CloseCurrentPopup();
          }

          ImGui::EndPopup();
        }
        ImGui::PopID();

        // Watch variable type
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( iter.m_strType.c_str() );

        // Watch variable value
        ImGui::TableNextColumn();

        // Preview the results if there are more than 3 lines
        const auto szOffset{ FindNthOccurrence( iter.m_strValue, "\n", NUM_VARIABLE_PREVIEW_LINES ) };
        if( szOffset == std::string::npos )
        {
          ImGui::TextUnformatted( iter.m_strValue.c_str() );
        }
        else
        {
          ImGui::TextUnformatted( iter.m_strValue.substr( 0, szOffset ).c_str() );
          DoVariableExpansion( iter );
        }
      }

      ImGui::TableNextRow();

      static char strCNewWatchVariable[MAX_FILENAME_LENGTH];

      ImGui::TableNextColumn();
      ImGui::TextUnformatted( "+" );
      ImGui::SameLine();

      auto pcContext{ rumDebugVM::GetCurrentDebugContext() };
      if( pcContext->m_bPaused )
      {
        ImGui::PushItemWidth( ImGui::GetColumnWidth() );

        if( ImGui::InputText( "##NewWatchVariable", strCNewWatchVariable, IM_ARRAYSIZE( strCNewWatchVariable ),
                              ImGuiInputTextFlags_EnterReturnsTrue ) )
        {
          if( rumDebugVM::WatchVariableAdd( strCNewWatchVariable ) )
          {
            rumDebugVM::s_cvDebugLock.notify_all();
          }

          memset( strCNewWatchVariable, '\0', sizeof( char ) * MAX_FILENAME_LENGTH );
        }

        ImGui::PopItemWidth();
      }

      // WatchTable
      ImGui::EndTable();
    }

    // WatchedTabChild
    ImGui::EndChild();

    // WatchedTabItem
    ImGui::EndTabItem();
  }
}

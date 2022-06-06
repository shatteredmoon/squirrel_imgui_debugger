#pragma once

#include <d_breakpoint.h>
#include <d_context.h>
#include <d_file.h>
#include <d_variable.h>

#include <squirrel.h>

#include <condition_variable>
#include <map>
#include <mutex>

// The VM Manager for the Squirrel ImGui Debugger. This static interface tracks attached VM Contexts, breakpoints, open
// files, and various variable groups. Code for this class is executed on the main thread and handles all Squirrel data
// access via requests from the Squirrel ImGui Interface, such as variables and callstack info.

class rumDebugVM
{
public:

  // The name and attachment state of a VM used by the interface thread
  struct rumVMInfo
  {
    std::string m_strName;
    bool m_bAttached{ false };
  };

  static SQInteger AttachVM( HSQUIRRELVM i_pVM );
  static SQInteger DetachVM( HSQUIRRELVM i_pVM );

  static void BreakpointAdd( rumDebugBreakpoint i_cBreakpoint );
  static void BreakpointRemove( const rumDebugBreakpoint& i_rcBreakpoint );
  static void BreakpointToggle( const rumDebugBreakpoint& i_rcBreakpoint );

  static void EnableDebugInfo( HSQUIRRELVM i_pVM, bool i_bEnable = true );

  static void FileOpen( const std::filesystem::path& i_fsFilePath, uint32_t i_uiLine );
  static void FileClose( const std::filesystem::path& i_fsFilePath );

  static const std::vector<rumDebugBreakpoint> GetBreakpointsCopy()
  {
    return s_vBreakpoints;
  }

  static const std::vector<rumDebugBreakpoint>& GetBreakpointsRef()
  {
    return s_vBreakpoints;
  }

  static const rumDebugContext* GetCurrentDebugContext()
  {
    return s_vCurrentDebugContext;
  }

  static const std::vector<rumDebugVariable>& GetLocalVariablesRef()
  {
    return s_vLocalVariables;
  }

  static const std::map<std::string, rumDebugFile> GetOpenedFilesCopy()
  {
    return s_mapOpenedFiles;
  }

  static const std::map<std::string, rumDebugFile>& GetOpenedFilesRef()
  {
    return s_mapOpenedFiles;
  }

  static const std::vector<rumVMInfo> GetVMInfo()
  {
    std::vector<rumVMInfo> vVMInfo;
    vVMInfo.reserve( s_mapRegisteredVMs.size() );

    for( const auto& iter : s_mapRegisteredVMs )
    {
      rumVMInfo cInfo;
      cInfo.m_strName = iter.second;
      cInfo.m_bAttached = IsDebuggerAttached( iter.first );

      vVMInfo.push_back(std::move(cInfo));
    }

    return vVMInfo;
  }

  static const std::vector<rumDebugVariable>& GetRequestedVariablesRef()
  {
    return s_vRequestedVariables;
  }

  static const std::vector<rumDebugVariable> GetWatchedVariablesCopy()
  {
    return s_vWatchVariables;
  }

  static const std::vector<rumDebugVariable>& GetWatchedVariablesRef()
  {
    return s_vWatchVariables;
  }

  static SQInteger IsDebuggerAttached( HSQUIRRELVM i_pVM );

  static void RegisterVM( HSQUIRRELVM i_pVM, const std::string& i_strName );

  static void RequestAttachVM( const std::string& i_strName );
  static void RequestDetachVM( const std::string& i_strName );

  static void RequestChangeStackLevel( uint32_t i_uiStackLevel );

  static void RequestResume();
  static void RequestStepInto();
  static void RequestStepOut();
  static void RequestStepOver();

  static void RequestVariable( const rumDebugVariable& i_rcVariable );
  static void RequestVariableUpdates();

  static void Update();

  static bool WatchVariableAdd( const std::string& i_strName );
  static bool WatchVariableEdit( const rumDebugVariable& i_rcVariable, const std::string& i_strEdit );
  static void WatchVariableRemove( const rumDebugVariable& i_rcVariable );

  // Holds the mutex lock state for cross-thread communication
  static std::condition_variable s_cvDebugLock;

private:

  // The VM must be registered by name in order to use these
  static void AttachVM( const std::string& i_strName );
  static void DetachVM( const std::string& i_strName );

  static void BuildLocalVariables( HSQUIRRELVM i_pVM, int32_t i_StackLevel );
  static void BuildVariables( HSQUIRRELVM i_pVM, std::vector<rumDebugVariable>& io_vVariables );

  static HSQUIRRELVM GetVMByName( const std::string& i_strName );

  static void NativeDebugHook( HSQUIRRELVM const i_pVM, const SQInteger i_eHookType, const SQChar* i_strFileName,
                               const SQInteger i_iLine, const SQChar* const i_strFunctionName );

  static void SuspendVM( HSQUIRRELVM i_pVM, rumDebugContext& i_rcContext, uint32_t i_uiLine,
                         const std::filesystem::path& i_fsFilePath );

  // All of the currently attached VMs
  static std::vector<rumDebugContext> s_vDebugContexts;

  // VMs that are awaiting attach/detach state changes
  static std::string s_strAttachRequest;
  static std::string s_strDetachRequest;

  static rumDebugContext* s_vCurrentDebugContext;

  // The current VM that is being debugged
  static HSQUIRRELCONSTVM s_pCurrentVM;

  // Registered VM mapping to user-provided name
  static std::map<HSQUIRRELVM, std::string> s_mapRegisteredVMs;

  // Currently set breakpoints
  static std::vector<rumDebugBreakpoint> s_vBreakpoints;

  // Currently opened files
  static std::map<std::string, rumDebugFile> s_mapOpenedFiles;

  // The current stack level used to parse local variables
  static uint32_t s_uiLocalVariableStackLevel;

  // Local variables
  static std::vector<rumDebugVariable> s_vLocalVariables;

  // Watched variables
  static std::vector<rumDebugVariable> s_vWatchVariables;

  // Requested variables - added to watch or hovered over during a pause
  static std::vector<rumDebugVariable> s_vRequestedVariables;

  // The lock used when updating shared information
  static std::mutex s_mtxAccessLock;

  // The lock used to pause execution of the main thread
  static std::mutex s_mtxDebugLock;
};

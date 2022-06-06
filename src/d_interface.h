#pragma once

#include <filesystem>
#include <mutex>
#include <string>

struct ImGuiContext;
struct ImGuiSettingsHandler;
struct ImGuiTextBuffer;
struct rumDebugVariable;

// The interactable ImGui interface by which users debug attached Squirrel VMs. Through this interface, users can
// control which files are open, view file contents, query program variables and stack info, set breakpoints, and step
// through code when execution is paused.

class rumDebugInterface
{
public:

  static void Init( const std::string& i_strName, uint32_t i_iPort, const std::string& i_strScriptPath );
  static void Shutdown();
  static void Update();

  static void RequestSettingsUpdate();
  static void SetFileFocus( const std::filesystem::path& s_fsFocusFile, int32_t s_iFocusLine );

  static bool WantsValuesAsHex()
  {
    return s_bShowHex;
  }

private:

  static void DisplayCode( const std::string& i_strSource, uint32_t i_uiLine,
                           bool i_bInMultilineComment );
  static void DisplayVariable( const rumDebugVariable& i_rcVariable );

  static void DoVariableExpansion( const rumDebugVariable& i_rcVariable );

  static size_t FindNthOccurrence( const std::string& i_strSource, const std::string& i_strFind,
                                   size_t i_szOccurence, size_t i_szOffset = 0 );

  static rumDebugVariable GetVariable( const std::string& i_strName );

  static void Settings_ReadLine( ImGuiContext* i_pContext, ImGuiSettingsHandler* i_pSettingsHandler, void* i_pEntry,
                                 const char* i_strLine );
  static void* Settings_ReadOpen( ImGuiContext* i_pContext, ImGuiSettingsHandler* i_pSettingsHandler,
                                  const char* i_strName );
  static void Settings_WriteAll( ImGuiContext* i_pContext, ImGuiSettingsHandler* i_pSettingsHandler,
                                 ImGuiTextBuffer* io_pBuffer );
  static void UpdateBreakpointTab();
  static void UpdateDisplayFolder( const std::string& i_strFolder,
                                   std::filesystem::recursive_directory_iterator& i_rcIter,
                                   std::filesystem::recursive_directory_iterator& i_rcIterEnd,
                                   const std::string& i_strFilter );
  static void UpdateFileExplorer();
  static void UpdateKeyDirectives();
  static void UpdateLocalsTab();
  static void UpdatePrimaryRow();
  static void UpdateSecondaryRow();
  static void UpdateSettings();
  static void UpdateSkipChildren( const std::string& i_strFolder,
                                  std::filesystem::recursive_directory_iterator& i_rcIter,
                                  std::filesystem::recursive_directory_iterator& i_rcIterEnd );
  static void UpdateSourceCode();
  static void UpdateStackBreakpointWindow();
  static void UpdateStackTab();
  static void UpdateVMsTab();
  static void UpdateWatchLocalWindow();
  static void UpdateWatchTab();

  // The user-set path to debuggable scripts
  static std::string s_strScriptPath;

  // The file that should have tab focus
  static std::filesystem::path s_fsFocusFile;

  // The line that should be shown
  static int32_t s_iFocusLine;

  // Lock guard
  static std::mutex s_mtxLockGuard;

  // Breakpoint colors
  using ImU32 = unsigned int;
  static ImU32 s_uiEnabledBreakpointColor;
  static ImU32 s_uiDisabledBreakpointColor;

  // Should settings be saved to .ini?
  static bool s_bUpdateSettings;

  // Show integer values as hex
  static bool s_bShowHex;
};

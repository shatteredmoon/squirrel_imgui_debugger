#pragma once

#include <squirrel.h>

#include <filesystem>
#include <queue>
#include <vector>

using HSQUIRRELCONSTVM = SQVM const*;

// Represents a Squirrel VM Context that has been attached to the VM Manager, and its various state attributes

struct rumDebugContext
{
  rumDebugContext( HSQUIRRELVM i_pVM ) : m_pVM( i_pVM )
  {}

  bool operator==( HSQUIRRELCONSTVM i_pVM ) const
  {
    return( m_pVM == i_pVM );
  }

  enum class StepDirective
  {
    Resume,
    StepOver,
    StepInto,
    StepOut
  };

  struct CallstackEntry
  {
    int32_t m_iLine{ 0 };
    std::string m_strFilename;
    std::string m_strFunction;
  };

  // The last known callstack
  std::vector<CallstackEntry> m_vCallstack;

  // Whether or not the VM is paused
  bool m_bPaused{ false };

  // The current line the VM is paused at
  uint32_t m_uiPausedLine{ 0 };

  // The current file the VM is paused on
  std::filesystem::path m_fsPausedFile;

  // The last issued step directive
  StepDirective m_eStepDirective{ StepDirective::Resume };

  // The last known stack level when a step directive was issued
  uint32_t m_uiStepDirectiveStackLevel{ 0 };

  // Has there been a variable request or stack level change?
  bool m_bUpdateVariables{ false };

  // Should the update focus on the paused instruction pointer?
  bool m_bFocusOnCurrentInstruction{ false };

private:

  // A pointer to the VM
  HSQUIRRELVM m_pVM{ nullptr };
};

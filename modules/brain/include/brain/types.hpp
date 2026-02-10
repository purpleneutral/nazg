#pragma once
#include <string>
#include <vector>

namespace nazg::brain {

// Decision types
enum class Action {
  SKIP,    // No action needed
  BUILD,   // Run build
  TEST,    // Run tests
  CLEAN,   // Clean then build
  UNKNOWN  // Can't determine
};

// Planning result
struct Plan {
  Action action = Action::UNKNOWN;
  std::string reason;              // Human-readable reason
  std::string command;             // Command to execute (if action != SKIP)
  std::vector<std::string> args;   // Command arguments
  std::string working_dir;         // Where to run command

  // Test-specific fields
  std::string test_framework;      // Framework to use (gtest, pytest, etc.)
  bool run_after_build = false;    // Chain BUILD → TEST
};

} // namespace nazg::brain

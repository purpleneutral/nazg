// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 purpleneutral
//
// This file is part of nazg.
//
// nazg is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// nazg is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along
// with nazg. If not, see <https://www.gnu.org/licenses/>.

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

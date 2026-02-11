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
#include <cstdint>
#include <string>
#include <string_view>

namespace nazg::system {

struct CommandResult {
  int exit_code = -1;
  std::string output;
};

// Execute command and capture stdout/stderr
std::string run_capture(const std::string &cmd);

// Execute command and return exit code
int run_command(const std::string &cmd);

// Execute command once, capturing combined stdout/stderr and the exit code.
CommandResult run_command_capture(const std::string &cmd);

// Execute command with a timeout (milliseconds).
// If the child exceeds the deadline it is killed (SIGTERM then SIGKILL).
// Returns exit_code 124 on timeout (matching coreutils timeout convention).
CommandResult run_command_with_timeout(const std::string &cmd,
                                       int64_t timeout_ms);

// Safely quote a shell argument using single quotes.
// Returns a string wrapped in single quotes with any existing
// single quotes escaped so that the result can be concatenated
// into a POSIX shell command without risk of injection.
std::string shell_quote(std::string_view value);

} // namespace nazg::system

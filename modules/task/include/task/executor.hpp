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
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::task {

// Execution result
struct ExecutionResult {
  int exit_code = 0;
  int64_t duration_ms = 0;
  std::string stdout_output;
  std::string stderr_output;
  bool success = false;
  std::string error_message;
};

// Execute commands and capture output
class Executor {
public:
  explicit Executor(nazg::blackbox::logger *log = nullptr);

  // Execute a command and capture output
  ExecutionResult execute(const std::string &command,
                          const std::vector<std::string> &args,
                          const std::string &working_dir = "");

  // Execute a shell command string
  ExecutionResult execute_shell(const std::string &command,
                                const std::string &working_dir = "");

  // Set timeout (0 = no timeout)
  void set_timeout(int64_t timeout_ms);

private:
  nazg::blackbox::logger *log_;
  int64_t timeout_ms_ = 0;
};

} // namespace nazg::task

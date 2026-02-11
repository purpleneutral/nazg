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

#include "task/executor.hpp"
#include "blackbox/logger.hpp"
#include "system/process.hpp"

#include <chrono>
#include <sstream>

namespace nazg::task {

namespace {

// Build a shell command that runs in the given working directory.
// The cd is scoped to the child process (popen/fork), so it is thread-safe.
std::string with_workdir(const std::string &cmd, const std::string &dir) {
  if (dir.empty()) {
    return cmd;
  }
  return "cd " + nazg::system::shell_quote(dir) + " && " + cmd;
}

} // namespace

Executor::Executor(nazg::blackbox::logger *log) : log_(log) {}

ExecutionResult Executor::execute(const std::string &command,
                                   const std::vector<std::string> &args,
                                   const std::string &working_dir) {
  ExecutionResult result;
  auto start = std::chrono::steady_clock::now();

  // Build full command
  std::ostringstream cmd;
  cmd << command;
  for (const auto &arg : args) {
    cmd << " " << arg;
  }
  std::string full_cmd = with_workdir(cmd.str() + " 2>&1", working_dir);

  if (log_) {
    log_->info("Task", "Executing: " + cmd.str());
    if (!working_dir.empty()) {
      log_->debug("Task", "Working dir: " + working_dir);
    }
  }

  // Execute command and capture output
  nazg::system::CommandResult cr;
  if (timeout_ms_ > 0) {
    cr = nazg::system::run_command_with_timeout(full_cmd, timeout_ms_);
  } else {
    cr = nazg::system::run_command_capture(full_cmd);
  }

  result.exit_code = cr.exit_code;
  result.stdout_output = cr.output;

  auto end = std::chrono::steady_clock::now();
  result.duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  result.success = (result.exit_code == 0);

  if (log_) {
    if (result.success) {
      log_->info("Task", "Completed in " + std::to_string(result.duration_ms) + "ms (exit: " +
                         std::to_string(result.exit_code) + ")");
    } else {
      log_->error("Task", "Failed with exit code " + std::to_string(result.exit_code));
    }
  }

  return result;
}

ExecutionResult Executor::execute_shell(const std::string &command,
                                        const std::string &working_dir) {
  ExecutionResult result;
  auto start = std::chrono::steady_clock::now();

  std::string full_cmd = with_workdir(command + " 2>&1", working_dir);

  if (log_) {
    log_->info("Task", "Executing shell: " + command);
    if (!working_dir.empty()) {
      log_->debug("Task", "Working dir: " + working_dir);
    }
  }

  // Execute command and capture output
  nazg::system::CommandResult cr;
  if (timeout_ms_ > 0) {
    cr = nazg::system::run_command_with_timeout(full_cmd, timeout_ms_);
  } else {
    cr = nazg::system::run_command_capture(full_cmd);
  }

  result.exit_code = cr.exit_code;
  result.stdout_output = cr.output;

  auto end = std::chrono::steady_clock::now();
  result.duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  result.success = (result.exit_code == 0);

  if (log_) {
    if (result.success) {
      log_->info("Task", "Completed in " + std::to_string(result.duration_ms) + "ms (exit: " +
                         std::to_string(result.exit_code) + ")");
    } else {
      log_->error("Task", "Failed with exit code " + std::to_string(result.exit_code));
    }
  }

  return result;
}

void Executor::set_timeout(int64_t timeout_ms) {
  timeout_ms_ = timeout_ms;
}

} // namespace nazg::task

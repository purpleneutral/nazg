#include "task/executor.hpp"
#include "blackbox/logger.hpp"
#include "system/process.hpp"

#include <chrono>
#include <sstream>
#include <unistd.h>

namespace nazg::task {

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
  std::string full_cmd = cmd.str();

  if (log_) {
    log_->info("Task", "Executing: " + full_cmd);
    if (!working_dir.empty()) {
      log_->debug("Task", "Working dir: " + working_dir);
    }
  }

  // Change to working directory if specified
  std::string original_dir;
  if (!working_dir.empty()) {
    char cwd[4096];
    if (::getcwd(cwd, sizeof(cwd))) {
      original_dir = cwd;
    } else {
      if (log_) {
        log_->warn("Task", "Failed to get current directory, cannot restore after execution");
      }
    }
    if (::chdir(working_dir.c_str()) != 0) {
      result.error_message = "Failed to change to directory: " + working_dir;
      result.success = false;
      if (log_) {
        log_->error("Task", result.error_message);
      }
      return result;
    }
    if (log_) {
      log_->debug("Task", "Changed to working directory: " + working_dir);
    }
  }

  // Execute command
  result.exit_code = nazg::system::run_command(full_cmd);

  // Restore directory
  if (!original_dir.empty()) {
    if (::chdir(original_dir.c_str()) != 0 && log_) {
      log_->warn("Task", "Failed to restore original directory: " + original_dir);
    }
  }

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

  if (log_) {
    log_->info("Task", "Executing shell: " + command);
    if (!working_dir.empty()) {
      log_->debug("Task", "Working dir: " + working_dir);
    }
  }

  // Change to working directory if specified
  std::string original_dir;
  if (!working_dir.empty()) {
    char cwd[4096];
    if (::getcwd(cwd, sizeof(cwd))) {
      original_dir = cwd;
    } else {
      if (log_) {
        log_->warn("Task", "Failed to get current directory, cannot restore after execution");
      }
    }
    if (::chdir(working_dir.c_str()) != 0) {
      result.error_message = "Failed to change to directory: " + working_dir;
      result.success = false;
      if (log_) {
        log_->error("Task", result.error_message);
      }
      return result;
    }
    if (log_) {
      log_->debug("Task", "Changed to working directory: " + working_dir);
    }
  }

  // Execute command directly through system() which handles shell properly
  result.exit_code = nazg::system::run_command(command);

  // Restore directory
  if (!original_dir.empty()) {
    if (::chdir(original_dir.c_str()) != 0 && log_) {
      log_->warn("Task", "Failed to restore original directory: " + original_dir);
    }
  }

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

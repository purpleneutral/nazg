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

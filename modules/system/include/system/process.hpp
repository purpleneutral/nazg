#pragma once
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

// Safely quote a shell argument using single quotes.
// Returns a string wrapped in single quotes with any existing
// single quotes escaped so that the result can be concatenated
// into a POSIX shell command without risk of injection.
std::string shell_quote(std::string_view value);

} // namespace nazg::system

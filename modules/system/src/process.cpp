#include "system/process.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#ifdef __unix__
#include <sys/wait.h>
#endif

namespace nazg::system {

namespace {

CommandResult exec_command(const std::string &cmd) {
  CommandResult result{};
  std::array<char, 256> buf{};

  FILE *p = popen(cmd.c_str(), "r");
  if (!p) {
    result.exit_code = -1;
    return result;
  }

  while (fgets(buf.data(), buf.size(), p)) {
    result.output.append(buf.data());
  }

  int status = pclose(p);
  if (status == -1) {
    result.exit_code = -1;
  }
#ifdef __unix__
  else if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else {
    result.exit_code = status;
  }
#else
  else {
    result.exit_code = status;
  }
#endif

  // Trim trailing newline for consistency with legacy behaviour
  if (!result.output.empty() && result.output.back() == '\n') {
    result.output.pop_back();
  }

  return result;
}

} // namespace

std::string run_capture(const std::string &cmd) {
  return exec_command(cmd).output;
}

int run_command(const std::string &cmd) {
  int status = std::system(cmd.c_str());
  if (status == -1) {
    return -1;
  }
#ifdef __unix__
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return status;
#else
  return status;
#endif
}

CommandResult run_command_capture(const std::string &cmd) {
  return exec_command(cmd);
}

std::string shell_quote(std::string_view value) {
  std::string quoted;
  quoted.reserve(value.size() + 2);
  quoted.push_back('\'');
  for (char ch : value) {
    if (ch == '\'') {
      quoted.append("'\"'\"'");
    } else {
      quoted.push_back(ch);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

} // namespace nazg::system

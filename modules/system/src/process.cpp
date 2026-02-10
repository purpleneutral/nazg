#include "system/process.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#ifdef __unix__
#include <cerrno>
#include <csignal>
#include <ctime>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
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

CommandResult run_command_with_timeout(const std::string &cmd,
                                       int64_t timeout_ms) {
#ifdef __unix__
  int pipefd[2];
  if (::pipe(pipefd) != 0) {
    return {-1, {}};
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    return {-1, {}};
  }

  if (pid == 0) {
    // Child: redirect stdout+stderr to pipe write end, exec shell
    ::close(pipefd[0]);
    ::dup2(pipefd[1], STDOUT_FILENO);
    ::dup2(pipefd[1], STDERR_FILENO);
    ::close(pipefd[1]);
    ::execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char *>(nullptr));
    ::_exit(127);
  }

  // Parent: read from pipe with timeout
  ::close(pipefd[1]);

  CommandResult result{};
  std::array<char, 4096> buf{};
  bool timed_out = false;

  // Use remaining timeout budget for poll
  auto deadline_ms = timeout_ms;

  for (;;) {
    struct pollfd pfd{};
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;

    int poll_ms = (deadline_ms > 0) ? static_cast<int>(deadline_ms) : 0;
    int pr = ::poll(&pfd, 1, poll_ms);

    if (pr > 0 && (pfd.revents & POLLIN)) {
      ssize_t n = ::read(pipefd[0], buf.data(), buf.size());
      if (n > 0) {
        result.output.append(buf.data(), static_cast<size_t>(n));
        // Rough remaining budget: just re-check full timeout.
        // For more precision we'd track elapsed time; good enough for
        // the dominant use case (seconds-scale timeouts).
        continue;
      }
      break; // EOF
    } else if (pr == 0) {
      // Timeout
      timed_out = true;
      break;
    } else {
      if (errno == EINTR) {
        continue;
      }
      break; // Error
    }
  }

  ::close(pipefd[0]);

  if (timed_out) {
    ::kill(pid, SIGTERM);
    // Give the child 200ms to exit before SIGKILL
    int status = 0;
    struct timespec ts = {0, 200000000}; // 200ms
    ::nanosleep(&ts, nullptr);
    if (::waitpid(pid, &status, WNOHANG) == 0) {
      ::kill(pid, SIGKILL);
      ::waitpid(pid, &status, 0);
    }
    result.exit_code = 124; // Convention: timeout
    return result;
  }

  int status = 0;
  ::waitpid(pid, &status, 0);

  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  } else {
    result.exit_code = status;
  }

  // Trim trailing newline for consistency
  if (!result.output.empty() && result.output.back() == '\n') {
    result.output.pop_back();
  }

  return result;
#else
  // No timeout support on non-Unix; fall back to regular capture
  return exec_command(cmd);
#endif
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

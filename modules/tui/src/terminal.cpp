#include "tui/terminal.hpp"
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#else
#error "Unsupported platform"
#endif

namespace nazg::tui {

Terminal::Terminal(const std::string& cmd,
                   const std::vector<std::string>& args,
                   int width,
                   int height)
    : cmd_(cmd), args_(args) {
  create_pty(width, height);
  spawn_process();
}

Terminal::~Terminal() {
  cleanup();
}

Terminal::Terminal(Terminal&& other) noexcept
    : master_fd_(other.master_fd_),
      child_pid_(other.child_pid_),
      cmd_(std::move(other.cmd_)),
      args_(std::move(other.args_)) {
  other.master_fd_ = -1;
  other.child_pid_ = -1;
}

Terminal& Terminal::operator=(Terminal&& other) noexcept {
  if (this != &other) {
    cleanup();
    master_fd_ = other.master_fd_;
    child_pid_ = other.child_pid_;
    cmd_ = std::move(other.cmd_);
    args_ = std::move(other.args_);
    other.master_fd_ = -1;
    other.child_pid_ = -1;
  }
  return *this;
}

void Terminal::create_pty(int width, int height) {
  struct winsize ws;
  ws.ws_col = width;
  ws.ws_row = height;
  ws.ws_xpixel = 0;
  ws.ws_ypixel = 0;

  int slave_fd = -1;
  if (openpty(&master_fd_, &slave_fd, nullptr, nullptr, &ws) == -1) {
    throw std::runtime_error(std::string("openpty failed: ") + strerror(errno));
  }

  // Make master non-blocking
  int flags = fcntl(master_fd_, F_GETFL, 0);
  fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);

  // Close slave in parent - child will reopen it
  close(slave_fd);
}

void Terminal::spawn_process() {
  child_pid_ = fork();

  if (child_pid_ == -1) {
    throw std::runtime_error(std::string("fork failed: ") + strerror(errno));
  }

  if (child_pid_ == 0) {
    // Child process
    // Create new session
    if (setsid() == -1) {
      _exit(1);
    }

    // Get slave PTY name and reopen it
    char* slave_name = ptsname(master_fd_);
    if (!slave_name) {
      _exit(1);
    }

    int slave_fd = open(slave_name, O_RDWR);
    if (slave_fd == -1) {
      _exit(1);
    }

    // Make this the controlling terminal
#ifdef TIOCSCTTY
    if (ioctl(slave_fd, TIOCSCTTY, nullptr) == -1) {
      _exit(1);
    }
#endif

    // Redirect stdin/stdout/stderr to slave PTY
    dup2(slave_fd, STDIN_FILENO);
    dup2(slave_fd, STDOUT_FILENO);
    dup2(slave_fd, STDERR_FILENO);

    if (slave_fd > STDERR_FILENO) {
      close(slave_fd);
    }

    // Close master fd in child
    close(master_fd_);

    // Build argv for exec
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(cmd_.c_str()));
    for (const auto& arg : args_) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    // Set TERM environment variable
    setenv("TERM", "xterm-256color", 1);

    // Execute the command
    execvp(cmd_.c_str(), argv.data());

    // If exec fails
    _exit(1);
  }

  // Parent process continues here
}

bool Terminal::is_alive() const {
  if (child_pid_ <= 0) {
    return false;
  }

  int status;
  pid_t result = waitpid(child_pid_, &status, WNOHANG);

  if (result == 0) {
    // Child is still running
    return true;
  } else if (result == child_pid_) {
    // Child has exited
    return false;
  } else {
    // Error
    return false;
  }
}

void Terminal::resize(int width, int height) {
  if (master_fd_ == -1) {
    return;
  }

  struct winsize ws;
  ws.ws_col = width;
  ws.ws_row = height;
  ws.ws_xpixel = 0;
  ws.ws_ypixel = 0;

  ioctl(master_fd_, TIOCSWINSZ, &ws);

  // Send SIGWINCH to child
  if (child_pid_ > 0) {
    ::kill(child_pid_, SIGWINCH);
  }
}

ssize_t Terminal::write(const std::string& data) {
  if (master_fd_ == -1) {
    return -1;
  }

  return ::write(master_fd_, data.c_str(), data.size());
}

ssize_t Terminal::read(char* buffer, size_t size) {
  if (master_fd_ == -1) {
    return -1;
  }

  ssize_t n = ::read(master_fd_, buffer, size);

  if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // No data available (non-blocking)
    return 0;
  }

  return n;
}

void Terminal::kill(int signal) {
  if (child_pid_ > 0) {
    ::kill(child_pid_, signal);
  }
}

void Terminal::cleanup() {
  if (child_pid_ > 0) {
    ::kill(child_pid_, SIGTERM);

    for (int i = 0; i < 10; ++i) {
      int status;
      if (waitpid(child_pid_, &status, WNOHANG) == child_pid_) {
        break;
      }
      usleep(10000); // 10ms
    }

    if (is_alive()) {
      ::kill(child_pid_, SIGKILL);
      waitpid(child_pid_, nullptr, 0);
    }

    child_pid_ = -1;
  }

  if (master_fd_ != -1) {
    close(master_fd_);
    master_fd_ = -1;
  }
}

} // namespace nazg::tui

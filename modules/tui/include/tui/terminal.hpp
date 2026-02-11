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
#include <sys/types.h>

namespace nazg::tui {

/**
 * @brief Terminal manages a pseudo-terminal (PTY) and the child process running in it.
 *
 * This class handles:
 * - PTY creation (master/slave pair)
 * - Fork/exec of shell process
 * - I/O with the running process
 * - Terminal size management
 * - Process lifecycle
 */
class Terminal {
public:
  /**
   * @brief Create a terminal running the specified command
   * @param cmd Command to run (e.g., "/bin/zsh")
   * @param args Arguments for the command
   * @param width Initial terminal width in characters
   * @param height Initial terminal height in characters
   */
  Terminal(const std::string& cmd,
           const std::vector<std::string>& args,
           int width,
           int height);

  /**
   * @brief Destructor - cleans up PTY and terminates child process
   */
  ~Terminal();

  // Non-copyable
  Terminal(const Terminal&) = delete;
  Terminal& operator=(const Terminal&) = delete;

  // Movable
  Terminal(Terminal&& other) noexcept;
  Terminal& operator=(Terminal&& other) noexcept;

  /**
   * @brief Check if the child process is still alive
   */
  bool is_alive() const;

  /**
   * @brief Get the PID of the child process
   */
  pid_t pid() const { return child_pid_; }

  /**
   * @brief Get the master PTY file descriptor
   */
  int master_fd() const { return master_fd_; }

  /**
   * @brief Resize the terminal
   * @param width New width in characters
   * @param height New height in characters
   */
  void resize(int width, int height);

  /**
   * @brief Send input to the terminal
   * @param data Data to send
   * @return Number of bytes written, or -1 on error
   */
  ssize_t write(const std::string& data);

  /**
   * @brief Read output from the terminal (non-blocking)
   * @param buffer Buffer to read into
   * @param size Maximum bytes to read
   * @return Number of bytes read, 0 if no data, -1 on error
   */
  ssize_t read(char* buffer, size_t size);

  /**
   * @brief Send signal to child process
   */
  void kill(int signal);

private:
  int master_fd_ = -1;
  pid_t child_pid_ = -1;
  std::string cmd_;
  std::vector<std::string> args_;

  /**
   * @brief Create and configure PTY
   */
  void create_pty(int width, int height);

  /**
   * @brief Fork and exec the child process
   */
  void spawn_process();

  /**
   * @brief Close PTY and clean up
   */
  void cleanup();
};

} // namespace nazg::tui

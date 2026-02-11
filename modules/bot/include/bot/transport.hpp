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
#include "bot/types.hpp"
#include <string>

namespace nazg::blackbox { class logger; }

namespace nazg::bot {

// SSH transport for remote bot execution
class SSHTransport {
public:
  explicit SSHTransport(const HostConfig& host, ::nazg::blackbox::logger* log = nullptr);

  // Execute command on remote host, return stdout
  std::string execute_command(const std::string& command, int& exit_code);

  // Execute script on remote host (stream script via stdin)
  std::string execute_script(const std::string& script_content, int& exit_code);

  // Get SSH connection string for logging
  std::string connection_string() const;

private:
  HostConfig host_;
  ::nazg::blackbox::logger* log_;

  // Build SSH command prefix
  std::string build_ssh_prefix() const;
};

class AgentTransport {
public:
  explicit AgentTransport(const HostConfig& host, ::nazg::blackbox::logger* log = nullptr);

  bool hello();
  bool execute_script(const std::string& script, int& exit_code,
                      std::string& stdout_output, std::string& stderr_output);

private:
  HostConfig host_;
  ::nazg::blackbox::logger* log_ = nullptr;
  int connect_timeout_ms_ = 2000;

  int connect_socket() const;
};

} // namespace nazg::bot

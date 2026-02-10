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

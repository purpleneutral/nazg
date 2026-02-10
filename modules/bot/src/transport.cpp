#include "bot/transport.hpp"

#include "agent/protocol.hpp"
#include "blackbox/logger.hpp"
#include "system/process.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

std::string extract_host(const std::string &address) {
  auto at = address.find('@');
  if (at == std::string::npos) {
    return address;
  }
  return address.substr(at + 1);
}

} // namespace

namespace nazg::bot {

SSHTransport::SSHTransport(const HostConfig& host, ::nazg::blackbox::logger* log)
    : host_(host), log_(log) {}

std::string SSHTransport::build_ssh_prefix() const {
  std::ostringstream oss;
  oss << "ssh";

  // Add port if non-standard
  if (host_.ssh_port != 22) {
    oss << " -p " << host_.ssh_port;
  }

  // Add SSH key if specified
  if (!host_.ssh_key.empty()) {
    oss << " -i " << ::nazg::system::shell_quote(host_.ssh_key);
  }

  // Add common SSH options for non-interactive use
  oss << " -o BatchMode=yes";           // Never ask for password
  oss << " -o StrictHostKeyChecking=no"; // Accept unknown hosts (bot use case)
  oss << " -o ConnectTimeout=10";       // 10 second timeout

  // Add target host
  oss << " " << ::nazg::system::shell_quote(host_.address);

  return oss.str();
}

std::string SSHTransport::execute_command(const std::string& command, int& exit_code) {
  std::string ssh_cmd = build_ssh_prefix() + " " + ::nazg::system::shell_quote(command);

  if (log_) {
    log_->info("bot.transport", "Executing SSH command on " + host_.address);
  }

  // Execute via system::run_capture
  std::string output;
  std::string full_cmd = ssh_cmd + " 2>&1";  // Capture both stdout and stderr

  // Use popen to capture output and get exit code
  FILE* pipe = popen(full_cmd.c_str(), "r");
  if (!pipe) {
    exit_code = -1;
    if (log_) {
      log_->error("bot.transport", "Failed to execute SSH command: popen failed");
    }
    return "";
  }

  std::array<char, 4096> buffer;
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    output += buffer.data();
  }

  int status = pclose(pipe);
  if (WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
  } else {
    exit_code = -1;
  }

  if (log_) {
    log_->debug("bot.transport", "SSH command completed with exit code " + std::to_string(exit_code));
  }

  return output;
}

std::string SSHTransport::execute_script(const std::string& script_content, int& exit_code) {
  // Stream script via stdin: printf '%s' "$SCRIPT" | ssh host 'bash -s'
  std::string ssh_cmd = build_ssh_prefix() + " 'bash -s'";

  if (log_) {
    log_->info("bot.transport", "Executing SSH script on " + host_.address);
  }

  // Create temporary command that pipes script to SSH using printf to preserve content
  std::ostringstream full_cmd;
  full_cmd << "printf '%s' " << ::nazg::system::shell_quote(script_content) << " | " << ssh_cmd << " 2>&1";

  if (log_) {
    log_->debug("bot.transport", "SSH command: " + full_cmd.str().substr(0, 200) + "...");
  }

  FILE* pipe = popen(full_cmd.str().c_str(), "r");
  if (!pipe) {
    exit_code = -1;
    if (log_) {
      log_->error("bot.transport", "Failed to execute SSH script: popen failed");
    }
    return "";
  }

  std::string output;
  std::array<char, 4096> buffer;
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    output += buffer.data();
  }

  int status = pclose(pipe);
  if (WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
  } else {
    exit_code = -1;
  }

  if (log_) {
    log_->debug("bot.transport", "SSH script completed with exit code " + std::to_string(exit_code));
  }

  return output;
}

std::string SSHTransport::connection_string() const {
  std::ostringstream oss;
  oss << host_.address;
  if (host_.ssh_port != 22) {
    oss << ":" << host_.ssh_port;
  }
  return oss.str();
}

AgentTransport::AgentTransport(const HostConfig &host, ::nazg::blackbox::logger *log)
    : host_(host), log_(log) {}

int AgentTransport::connect_socket() const {
  const std::string host_name = extract_host(host_.address);
  std::string port = std::to_string(host_.agent_port);

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo *result = nullptr;
  int rc = ::getaddrinfo(host_name.c_str(), port.c_str(), &hints, &result);
  if (rc != 0 || result == nullptr) {
    if (log_) {
      log_->debug("bot.transport", "Agent lookup failed: " + std::string(gai_strerror(rc)));
    }
    if (result) {
      ::freeaddrinfo(result);
    }
    return -1;
  }

  int fd = -1;
  for (addrinfo *ai = result; ai != nullptr; ai = ai->ai_next) {
    fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
      continue;
    }

    timeval timeout{};
    timeout.tv_sec = connect_timeout_ms_ / 1000;
    timeout.tv_usec = (connect_timeout_ms_ % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
      break;
    }

    ::close(fd);
    fd = -1;
  }

  ::freeaddrinfo(result);
  return fd;
}

bool AgentTransport::hello() {
  int fd = connect_socket();
  if (fd < 0) {
    return false;
  }

  using namespace ::nazg::agent::protocol;
  Header header{MessageType::Hello, 0};
  std::string payload = "nazg-controller";
  auto packet = encode(header, payload);

  bool success = false;

  if (::send(fd, packet.data(), packet.size(), 0) >= 0) {
    std::vector<std::uint8_t> buffer(sizeof(Header) + 256);
    ssize_t bytes = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (bytes > 0) {
      buffer.resize(static_cast<std::size_t>(bytes));
      Header ack_header{};
      std::string ack_payload;
      if (decode(buffer, ack_header, ack_payload) &&
          ack_header.type == MessageType::HelloAck) {
        success = true;
        if (log_) {
          log_->info("bot.transport", "Agent responded: " + ack_payload);
        }
      }
    }
  }

  ::close(fd);
  return success;
}

bool AgentTransport::execute_script(const std::string &script, int &exit_code,
                                    std::string &stdout_output, std::string &stderr_output) {
  exit_code = -1;
  stdout_output.clear();
  stderr_output.clear();

  int fd = connect_socket();
  if (fd < 0) {
    return false;
  }

  using namespace ::nazg::agent::protocol;
  auto send_message = [&](MessageType type, const std::string &payload) -> bool {
    Header header{type, 0};
    auto packet = encode(header, payload);
    std::size_t offset = 0;
    while (offset < packet.size()) {
      ssize_t written = ::send(fd, packet.data() + offset, packet.size() - offset, 0);
      if (written <= 0) {
        return false;
      }
      offset += static_cast<std::size_t>(written);
    }
    return true;
  };

  auto read_message = [&](Header &hdr, std::string &body) -> bool {
    hdr = Header{};
    body.clear();

    std::size_t header_bytes = 0;
    unsigned char *hdr_buf = reinterpret_cast<unsigned char *>(&hdr);
    while (header_bytes < sizeof(Header)) {
      ssize_t received = ::recv(fd, hdr_buf + header_bytes, sizeof(Header) - header_bytes, 0);
      if (received <= 0) {
        return false;
      }
      header_bytes += static_cast<std::size_t>(received);
    }

    if (hdr.payload_size == 0) {
      return true;
    }

    body.resize(hdr.payload_size);
    std::size_t offset = 0;
    while (offset < hdr.payload_size) {
      ssize_t received = ::recv(fd, body.data() + offset, hdr.payload_size - offset, 0);
      if (received <= 0) {
        return false;
      }
      offset += static_cast<std::size_t>(received);
    }
    return true;
  };

  if (!send_message(MessageType::Hello, "nazg-controller")) {
    ::close(fd);
    return false;
  }

  Header ack_header{};
  std::string ack_payload;
  if (!read_message(ack_header, ack_payload) || ack_header.type != MessageType::HelloAck) {
    ::close(fd);
    return false;
  }

  if (!send_message(MessageType::RunCommand, script)) {
    ::close(fd);
    return false;
  }

  // Expect result
  Header result_header{};
  std::string result_payload;
  bool ok = read_message(result_header, result_payload);
  ::close(fd);
  if (!ok || result_header.type != MessageType::Result) {
    return false;
  }

  auto newline = result_payload.find('\n');
  if (newline == std::string::npos) {
    return false;
  }
  try {
    exit_code = std::stoi(result_payload.substr(0, newline));
  } catch (...) {
    return false;
  }
  stdout_output = result_payload.substr(newline + 1);
  return true;
}

} // namespace nazg::bot

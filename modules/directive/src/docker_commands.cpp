#include "directive/docker_commands.hpp"

#include "directive/context.hpp"
#include "directive/registry.hpp"
#include "directive/agent_utils.hpp"

#include "agent/protocol.hpp"
#include "blackbox/logger.hpp"
#include "config/config.hpp"
#include "nexus/store.hpp"
#include "prompt/prompt.hpp"
#include "system/fs.hpp"
#include "system/process.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <netdb.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>
#include <termios.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

namespace nazg::directive {
namespace {

namespace fs = std::filesystem;

std::string trim_copy(std::string value) {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
    value.pop_back();
  return value;
}

std::string read_password(const std::string &prompt_text) {
  std::cout << prompt_text << ": " << std::flush;

  termios oldt{};
  if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
    std::string line;
    std::getline(std::cin, line);
    return line;
  }

  termios newt = oldt;
  newt.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  std::string password;
  std::getline(std::cin, password);

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  std::cout << "\n";
  return password;
}

std::string default_ssh_key_path(const directive::context &ectx) {
  if (ectx.cfg && ectx.cfg->has("bots", "ssh_key")) {
    auto value = ectx.cfg->get_string("bots", "ssh_key");
    if (!value.empty())
      return value;
  }
  if (const char *home = std::getenv("HOME")) {
    return std::string(home) + "/.ssh/id_ed25519";
  }
  return "~/.ssh/id_ed25519";
}

bool generate_ssh_key(const std::string &path, const directive::context &ectx) {
  fs::path key_path(path);
  std::error_code ec;
  if (!key_path.parent_path().empty())
    fs::create_directories(key_path.parent_path(), ec);

  std::string cmd = "ssh-keygen -t ed25519 -f " +
                    ::nazg::system::shell_quote(path) + " -N \"\" -q";
  if (ectx.log) {
    ectx.log->info("Server", "Generating SSH key: " + key_path.string());
  }
  int rc = ::nazg::system::run_command(cmd);
  if (rc != 0) {
    if (ectx.log) {
      ectx.log->error("Server", "ssh-keygen failed with exit code " +
                                   std::to_string(rc));
    }
    return false;
  }
  return fs::exists(key_path);
}

bool ensure_ssh_key_available(std::string &path, bool force_generate,
                              bool &generated, const directive::context &ectx) {
  generated = false;
  path = ::nazg::system::expand_tilde(path);
  fs::path key_path(path);

  if (fs::exists(key_path))
    return true;

  if (!force_generate)
    return false;

  if (!generate_ssh_key(path, ectx))
    return false;

  generated = true;
  return true;
}

struct SshConfigFields {
  std::string key;
  int port = 22;
  int agent_port = 7070;
};

struct RemoteContainerInfo {
  std::string name;
  std::string status;
  std::string image;
};

void print_kv_section(const std::string &title,
                      const std::vector<std::pair<std::string, std::string>> &rows) {
  if (rows.empty())
    return;
  std::cout << "\n== " << title << " ==" << std::endl;
  std::size_t width = 0;
  for (const auto &row : rows)
    width = std::max(width, row.first.size());
  for (const auto &row : rows) {
    std::cout << "  " << std::left << std::setw(static_cast<int>(width)) << row.first
              << " : " << row.second << std::endl;
  }
}

void print_bullet_section(const std::string &title,
                          const std::vector<std::string> &lines) {
  if (lines.empty())
    return;
  std::cout << "\n== " << title << " ==" << std::endl;
  for (const auto &line : lines)
    std::cout << "  • " << line << std::endl;
}

static std::string escape_json_string(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 4);
  for (char ch : value) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

static std::string extract_json_token(const std::string &json, std::string_view key) {
  auto pos = json.find('"' + std::string(key) + '"');
  if (pos == std::string::npos)
    return {};
  pos = json.find(':', pos);
  if (pos == std::string::npos)
    return {};
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
    ++pos;
  if (pos >= json.size())
    return {};

  if (json[pos] == '"') {
    ++pos;
    std::string value;
    while (pos < json.size()) {
      char ch = json[pos++];
      if (ch == '\\' && pos < json.size()) {
        value.push_back(json[pos++]);
        continue;
      }
      if (ch == '"')
        break;
      value.push_back(ch);
    }
    return value;
  }

  size_t start = pos;
  while (pos < json.size() && (std::isdigit(static_cast<unsigned char>(json[pos])) ||
                               json[pos] == '-' || json[pos] == '+')) {
    ++pos;
  }
  return json.substr(start, pos - start);
}

SshConfigFields parse_ssh_config_json(const std::string &json) {
  SshConfigFields cfg;
  if (json.empty())
    return cfg;

  std::string key = extract_json_token(json, "key");
  if (!key.empty())
    cfg.key = key;

  std::string port = extract_json_token(json, "port");
  if (!port.empty()) {
    try {
      cfg.port = std::stoi(port);
    } catch (...) {
    }
  }

  std::string agent_port = extract_json_token(json, "agent_port");
  if (!agent_port.empty()) {
    try {
      cfg.agent_port = std::stoi(agent_port);
    } catch (...) {
    }
  }

  return cfg;
}

std::string build_ssh_config_json(const SshConfigFields &cfg) {
  std::ostringstream oss;
  oss << "{\"key\":\"" << escape_json_string(cfg.key) << "\""
      << ",\"port\":" << cfg.port
      << ",\"agent_port\":" << cfg.agent_port << "}";
  return oss.str();
}

std::string extract_host_only(const std::string &address) {
  if (address.empty())
    return address;
  auto pos = address.rfind('@');
  std::string host = pos == std::string::npos ? address : address.substr(pos + 1);
  while (!host.empty() && std::isspace(static_cast<unsigned char>(host.front())))
    host.erase(host.begin());
  while (!host.empty() && std::isspace(static_cast<unsigned char>(host.back())))
    host.pop_back();
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
    host = host.substr(1, host.size() - 2);
  }
  return host;
}

bool run_agent_script(const std::string &host, int agent_port,
                      const std::string &script, int &exit_code,
                      std::string &stdout_output, std::string &error_message,
                      const directive::context &ectx) {
  (void)ectx;
  exit_code = -1;
  stdout_output.clear();
  error_message.clear();

  if (host.empty()) {
    error_message = "Server host is not configured";
    return false;
  }

  struct addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo *result = nullptr;
  std::string port_str = std::to_string(agent_port);
  int rc = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
  if (rc != 0) {
    error_message = std::string("getaddrinfo failed: ") + ::gai_strerror(rc);
    return false;
  }

  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> guard(result, ::freeaddrinfo);

  int fd = -1;
  for (addrinfo *ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
    fd = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (fd < 0)
      continue;

    timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (::connect(fd, ptr->ai_addr, ptr->ai_addrlen) == 0) {
      break;
    }

    ::close(fd);
    fd = -1;
  }

  if (fd < 0) {
    error_message = "Unable to connect to agent at " + host + ":" + port_str;
    return false;
  }

  auto close_fd = [&]() {
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  };

  using namespace nazg::agent::protocol;

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

  if (!send_message(MessageType::Hello, "nazg-docker-run")) {
    error_message = "Failed to send hello to agent";
    close_fd();
    return false;
  }

  Header header{};
  std::string payload;
  if (!read_message(header, payload) || header.type != MessageType::HelloAck) {
    error_message = "Agent did not acknowledge hello";
    close_fd();
    return false;
  }

  if (!send_message(MessageType::RunCommand, script)) {
    error_message = "Failed to send command to agent";
    close_fd();
    return false;
  }

  Header result_header{};
  std::string result_payload;
  if (!read_message(result_header, result_payload)) {
    error_message = "No response from agent";
    close_fd();
    return false;
  }

  if (result_header.type == MessageType::Error) {
    error_message = result_payload.empty() ? "Agent reported an error" : result_payload;
    close_fd();
    return false;
  }

  if (result_header.type != MessageType::Result) {
    error_message = "Unexpected response from agent";
    close_fd();
    return false;
  }

  auto newline = result_payload.find('\n');
  if (newline == std::string::npos) {
    error_message = "Malformed agent response";
    close_fd();
    return false;
  }

  try {
    exit_code = std::stoi(result_payload.substr(0, newline));
  } catch (...) {
    error_message = "Failed to parse agent exit code";
    close_fd();
    return false;
  }

  stdout_output = result_payload.substr(newline + 1);
  close_fd();
  return true;
}

bool check_agent_connection(const std::string &host, int agent_port,
                            std::string &error, const directive::context &ectx) {
  error.clear();
  if (host.empty()) {
    error = "host not configured";
    return false;
  }

  std::string stdout_output;
  int exit_code = -1;
  std::string agent_error;
  bool ok = run_agent_script(host, agent_port,
                             "#!/usr/bin/env bash\nexit 0\n",
                             exit_code, stdout_output, agent_error, ectx);
  if (!ok) {
    error = agent_error;
    return false;
  }
  if (exit_code != 0) {
    error = "agent exited " + std::to_string(exit_code);
    return false;
  }
  return true;
}

bool fetch_remote_containers(const std::string &host, int agent_port,
                             bool include_all,
                             std::vector<RemoteContainerInfo> &out,
                             std::string &error,
                             const directive::context &ectx) {
  out.clear();
  error.clear();

  std::ostringstream script;
  script << "#!/usr/bin/env bash\n"
         << "set -euo pipefail\n"
         << "PATH=$PATH:/usr/local/bin\n"
         << "docker ps " << (include_all ? "-a " : "")
         << "--format '{{.Names}}\t{{.Status}}\t{{.Image}}'\n";

  int exit_code = -1;
  std::string stdout_output;
  std::string agent_error;
  bool ok = run_agent_script(host, agent_port, script.str(), exit_code,
                             stdout_output, agent_error, ectx);
  if (!ok) {
    error = agent_error;
    return false;
  }
  if (exit_code != 0) {
    error = stdout_output.empty()
                ? std::string("docker ps exited with ") + std::to_string(exit_code)
                : stdout_output;
    return false;
  }

  std::istringstream iss(stdout_output);
  std::string line;
  while (std::getline(iss, line)) {
    if (line.empty())
      continue;
    std::istringstream row(line);
    RemoteContainerInfo info;
    if (!std::getline(row, info.name, '\t'))
      continue;
    if (!std::getline(row, info.status, '\t'))
      info.status.clear();
    if (!std::getline(row, info.image, '\t'))
      info.image.clear();
    out.push_back(std::move(info));
  }
  return true;
}

std::optional<fs::path> find_agent_binary(const std::string &override_path) {
  std::vector<fs::path> candidates;
  if (!override_path.empty()) {
    candidates.emplace_back(::nazg::system::expand_tilde(override_path));
  }

  fs::path cwd = fs::current_path();
  candidates.emplace_back(cwd / "build-self" / "nazg-agent");
  candidates.emplace_back(cwd / "build" / "nazg-agent");
  candidates.emplace_back(cwd / "nazg-agent");

  for (const auto &path : candidates) {
    std::error_code ec;
    if (!path.empty() && fs::exists(path, ec) && fs::is_regular_file(path, ec)) {
      return fs::absolute(path);
    }
  }
  return std::nullopt;
}

std::string build_ssh_prefix(const std::string &remote,
                             const SshConfigFields &cfg) {
  std::ostringstream ssh;
  ssh << "ssh -o BatchMode=yes -o StrictHostKeyChecking=no";
  std::string key = ::nazg::system::expand_tilde(cfg.key);
  if (!key.empty()) {
    ssh << " -i " << ::nazg::system::shell_quote(key);
  }
  if (cfg.port != 22) {
    ssh << " -p " << cfg.port;
  }
  ssh << ' ' << ::nazg::system::shell_quote(remote);
  return ssh.str();
}

std::string build_scp_prefix(const SshConfigFields &cfg) {
  std::ostringstream scp;
  scp << "scp -q -o StrictHostKeyChecking=no";
  std::string key = ::nazg::system::expand_tilde(cfg.key);
  if (!key.empty()) {
    scp << " -i " << ::nazg::system::shell_quote(key);
  }
  if (cfg.port != 22) {
    scp << " -P " << cfg.port;
  }
  return scp.str();
}

bool run_local_command(const std::string &description, const std::string &command,
                       const directive::context &ectx,
                       const std::string &display_command = {}) {
  std::cout << "→ " << description << std::endl;
  auto result = ::nazg::system::run_command_capture(command);
  if (result.exit_code != 0) {
    std::cerr << "Failed to " << description << " (exit code " << result.exit_code << ")\n";
    const std::string &cmd_to_show = display_command.empty() ? command : display_command;
    std::cerr << "Command: " << cmd_to_show << "\n";
    if (!result.output.empty()) {
      std::cerr << result.output << "\n";
    }
    return false;
  }

  if (ectx.verbose && !result.output.empty()) {
    std::cout << result.output;
    if (result.output.back() != '\n') {
      std::cout << '\n';
    }
  }
  return true;
}

bool launch_bot_setup(const directive::context &ectx, const std::string &label,
                      const std::string &remote_login, const SshConfigFields &cfg) {
  if (!ectx.reg) {
    std::cerr << "Registry unavailable; cannot launch bot setup." << std::endl;
    return false;
  }

  std::vector<std::string> storage;
  storage.reserve(12);
  storage.push_back(ectx.prog.empty() ? "nazg" : ectx.prog);
  storage.push_back("bot");
  storage.push_back("setup");
  storage.push_back("--host");
  storage.push_back(remote_login);
  if (!label.empty()) {
    storage.push_back("--label");
    storage.push_back(label);
  }
  if (!cfg.key.empty()) {
    storage.push_back("--key");
    storage.push_back(cfg.key);
  }
  if (cfg.port != 22) {
    storage.push_back("--port");
    storage.push_back(std::to_string(cfg.port));
  }
  if (cfg.agent_port != 7070) {
    storage.push_back("--agent-port");
    storage.push_back(std::to_string(cfg.agent_port));
  }

  std::vector<const char *> argv;
  argv.reserve(storage.size());
  for (auto &token : storage) {
    argv.push_back(token.c_str());
  }

  auto dispatch_result = ectx.reg->dispatch("bot", ectx, argv);
  if (!dispatch_result.first) {
    std::cerr << "Failed to dispatch bot setup command." << std::endl;
    return false;
  }
  return dispatch_result.second == 0;
}

std::string make_remote_command(const std::string &body, bool needs_sudo,
                                bool have_sudo_password,
                                const std::string &sudo_password) {
  if (!needs_sudo)
    return body;

  std::string wrapped = "sh -c " + ::nazg::system::shell_quote(body);
  if (have_sudo_password) {
    return std::string("echo ") + ::nazg::system::shell_quote(sudo_password) +
           " | sudo -S -p '' " + wrapped;
  }
  return std::string("sudo -p '' ") + wrapped;
}

std::string make_remote_display_command(const std::string &body, bool needs_sudo,
                                        bool have_sudo_password,
                                        const std::string &sudo_password) {
  if (!needs_sudo)
    return body;

  std::string wrapped = "sh -c " + ::nazg::system::shell_quote(body);
  if (have_sudo_password) {
    return "echo '***' | sudo -S -p '' " + wrapped;
  }
  return std::string("sudo -p '' ") + wrapped;
}

std::string map_get(const std::map<std::string, std::string> &row,
                    const std::string &key, const std::string &fallback = {}) {
  auto it = row.find(key);
  return it != row.end() ? it->second : fallback;
}

int64_t to_int64(const std::string &value, int64_t fallback = 0) {
  if (value.empty())
    return fallback;
  try {
    return std::stoll(value);
  } catch (...) {
    return fallback;
  }
}

std::string format_timestamp(int64_t epoch) {
  if (epoch <= 0)
    return "-";
  std::time_t tt = static_cast<std::time_t>(epoch);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif
  char buffer[32] = {0};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm))
    return buffer;
  return std::to_string(epoch);
}

std::string format_timestamp(const std::string &epoch_str) {
  return format_timestamp(to_int64(epoch_str));
}

std::map<int64_t, std::string> build_server_label_map(nexus::Store *store) {
  std::map<int64_t, std::string> map;
  if (!store)
    return map;
  for (const auto &server : store->list_servers()) {
    map[to_int64(map_get(server, "id"))] = map_get(server, "label");
  }
  return map;
}

std::optional<int64_t> resolve_server_id(nexus::Store *store,
                                         const std::string &label) {
  if (!store)
    return std::nullopt;
  auto info = store->get_server(label);
  if (!info)
    return std::nullopt;
  return to_int64(map_get(*info, "id"));
}

std::string escape_json(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 4);
  for (char ch : value) {
    if (ch == '"' || ch == '\\')
      out.push_back('\\');
    out.push_back(ch);
  }
  return out;
}

void print_server_list(nexus::Store *store) {
  auto servers = store ? store->list_servers() : std::vector<std::map<std::string, std::string>>{};
  if (servers.empty()) {
    std::cout << "No servers registered. Use 'nazg server add <label> <host>' to add one.\n";
    return;
  }

  std::cout << std::left
            << std::setw(16) << "Label"
            << std::setw(24) << "Host"
            << std::setw(12) << "Status"
            << std::setw(20) << "Last Heartbeat"
            << std::setw(24) << "Agent Version"
            << "Capabilities\n";
  std::cout << std::string(16 + 24 + 12 + 20 + 24 + 16, '-') << "\n";
  for (const auto &server : servers) {
    std::cout << std::setw(16) << map_get(server, "label")
              << std::setw(24) << map_get(server, "host")
              << std::setw(12) << map_get(server, "agent_status", "unknown")
              << std::setw(20) << format_timestamp(map_get(server, "last_heartbeat"))
              << std::setw(24) << map_get(server, "agent_version")
              << map_get(server, "capabilities") << "\n";
  }
}

void print_container_table(const std::vector<std::map<std::string, std::string>> &containers,
                           const std::map<int64_t, std::string> &server_labels) {
  if (containers.empty()) {
    std::cout << "No containers recorded." << std::endl;
    return;
  }

  std::cout << std::left
            << std::setw(16) << "Server"
            << std::setw(22) << "Name"
            << std::setw(16) << "State"
            << std::setw(16) << "Health"
            << std::setw(28) << "Image"
            << std::setw(28) << "Status"
            << "Last Seen" << "\n";
  std::cout << std::string(16 + 22 + 16 + 16 + 28 + 28 + 20, '-') << "\n";

  for (const auto &row : containers) {
    int64_t sid = to_int64(map_get(row, "server_id"));
    std::string server = server_labels.count(sid) ? server_labels.at(sid) : std::to_string(sid);
    std::cout << std::setw(16) << server
              << std::setw(22) << map_get(row, "name")
              << std::setw(16) << map_get(row, "state")
              << std::setw(16) << map_get(row, "health_status")
              << std::setw(28) << map_get(row, "image")
              << std::setw(28) << map_get(row, "status")
              << format_timestamp(map_get(row, "last_seen")) << "\n";
  }
}

void print_event_header() {
  std::cout << std::left
            << std::setw(6) << "ID"
            << std::setw(16) << "Server"
            << std::setw(24) << "Timestamp"
            << std::setw(22) << "Container"
            << std::setw(14) << "Event"
            << std::setw(16) << "Old"
            << std::setw(16) << "New"
            << "Metadata" << "\n";
  std::cout << std::string(6 + 16 + 24 + 22 + 14 + 16 + 16 + 20, '-') << "\n";
}

void print_events(const std::vector<std::map<std::string, std::string>> &events,
                  const std::map<int64_t, std::string> &server_labels) {
  if (events.empty()) {
    std::cout << "No events recorded." << std::endl;
    return;
  }

  print_event_header();
  for (const auto &row : events) {
    int64_t sid = to_int64(map_get(row, "server_id"));
    std::string server = server_labels.count(sid) ? server_labels.at(sid) : std::to_string(sid);
    std::cout << std::setw(6) << map_get(row, "id")
              << std::setw(16) << server
              << std::setw(24) << format_timestamp(map_get(row, "timestamp"))
              << std::setw(22) << map_get(row, "container_name")
              << std::setw(14) << map_get(row, "event_type")
              << std::setw(16) << map_get(row, "old_state")
              << std::setw(16) << map_get(row, "new_state")
              << map_get(row, "metadata") << "\n";
  }
}

void print_server_help() {
  std::cout << "Usage: nazg server <command> [options]\n"
            << "Commands:\n"
            << "  add <label> <host> [--ssh-key PATH] [--generate-key] [--port N] [--agent-port N]\n"
            << "  list\n"
            << "  status <label>\n"
            << "  install-agent <label> [options]\n";
}

int cmd_server_add(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  if (ctx.argc < 5) {
    std::cerr << "Usage: nazg server add <label> <host> [--ssh-key PATH] [--generate-key] [--port N] [--agent-port N]\n";
    return 1;
  }

  std::string label = ctx.argv[3];
  std::string host = ctx.argv[4];
  std::string ssh_key;
  bool generate_key = false;
  int port = 22;
  int agent_port = 7070;

  for (int i = 5; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if ((arg == "--ssh-key" || arg == "-k") && i + 1 < ctx.argc) {
      ssh_key = ctx.argv[++i];
    } else if (arg == "--generate-key") {
      generate_key = true;
    } else if (arg == "--port" && i + 1 < ctx.argc) {
      port = static_cast<int>(to_int64(ctx.argv[++i], port));
    } else if ((arg == "--agent-port" || arg == "-a") && i + 1 < ctx.argc) {
      agent_port = static_cast<int>(to_int64(ctx.argv[++i], agent_port));
    }
  }

  if (ssh_key.empty())
    ssh_key = default_ssh_key_path(ectx);

  bool generated = false;
  if (!ensure_ssh_key_available(ssh_key, generate_key, generated, ectx)) {
    std::cerr << "Error: SSH key " << ssh_key
              << " not found. Re-run with --generate-key to create one or provide an existing key via --ssh-key.\n";
    std::cerr << "Example: nazg server add " << label << " " << host
              << " --generate-key\n";
    return 1;
  }

  std::ostringstream ssh_json;
  SshConfigFields cfg_fields;
  cfg_fields.key = ssh_key;
  cfg_fields.port = port;
  cfg_fields.agent_port = agent_port;
  ssh_json << build_ssh_config_json(cfg_fields);

  bool updated_existing = false;
  int64_t id = 0;
  if (auto existing = ectx.store->get_server(label)) {
    id = to_int64(map_get(*existing, "id"));
    if (!ectx.store->update_server_connection(id, host, ssh_json.str())) {
      std::cerr << "Failed to update server. Check logs for details." << std::endl;
      return 1;
    }
    updated_existing = true;
  } else {
    id = ectx.store->add_server(label, host, ssh_json.str());
    if (id <= 0) {
      std::cerr << "Failed to add server. Check logs for details." << std::endl;
      return 1;
    }
  }

  if (generated) {
    std::cout << "Generated new SSH key at " << ssh_key << "\n";
  }

  if (updated_existing) {
    std::cout << "Updated server '" << label << "' (" << host << ")\n";
  } else {
    std::cout << "Registered server '" << label << "' (" << host << ") with id " << id << "\n";
  }
  std::cout << "Stored SSH key: " << ssh_key << "\n";
  return 0;
}

int cmd_server_list(const command_context &, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }
  print_server_list(ectx.store);
  return 0;
}

int cmd_server_status(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  if (ctx.argc < 4) {
    std::cerr << "Usage: nazg server status <label>\n";
    return 1;
  }

  std::string label = ctx.argv[3];
  auto server = ectx.store->get_server(label);
  if (!server) {
    std::cerr << "Server '" << label << "' not found. Use 'nazg server list'.\n";
    return 1;
  }

  SshConfigFields ssh_fields = parse_ssh_config_json(map_get(*server, "ssh_config"));
  std::string expanded_key = ssh_fields.key.empty()
                                ? std::string()
                                : ::nazg::system::expand_tilde(ssh_fields.key);
  bool key_present = !expanded_key.empty() && fs::exists(expanded_key);

  std::string host_value = map_get(*server, "host");
  std::string remote_host = extract_host_only(host_value);
  std::string ssh_target = host_value.empty() ? remote_host : host_value;
  std::string ssh_prefix;
  if (!ssh_target.empty())
    ssh_prefix = build_ssh_prefix(ssh_target, ssh_fields);

  std::vector<std::pair<std::string, std::string>> overview = {
      {"Label", map_get(*server, "label")},
      {"Host", host_value},
      {"Status", map_get(*server, "agent_status", "unknown")},
      {"Agent Version", map_get(*server, "agent_version")},
      {"Last Heartbeat", format_timestamp(map_get(*server, "last_heartbeat"))},
      {"Capabilities", map_get(*server, "capabilities")}};

  std::vector<std::pair<std::string, std::string>> connectivity = {
      {"SSH Port", std::to_string(ssh_fields.port)},
      {"Agent Port", std::to_string(ssh_fields.agent_port)},
      {"SSH Key",
       ssh_fields.key.empty() ? "(not configured)" : ssh_fields.key}};
  if (!ssh_fields.key.empty() && !key_present)
    connectivity.back().second += " (missing)";

  std::string stored_strategy = map_get(*server, "agent_container_strategy", "binary");
  std::string stored_local_tar = map_get(*server, "agent_container_local_tar");
  std::string stored_remote_tar = map_get(*server, "agent_container_remote_tar");
  std::string stored_container_image = map_get(*server, "agent_container_image");
  std::vector<std::pair<std::string, std::string>> deployment;
  if (stored_strategy != "binary" || !stored_container_image.empty() ||
      !stored_local_tar.empty() || !stored_remote_tar.empty()) {
    deployment.emplace_back("Strategy", stored_strategy);
    if (!stored_container_image.empty())
      deployment.emplace_back("Image", stored_container_image);
    if (!stored_local_tar.empty())
      deployment.emplace_back("Local Tar", stored_local_tar);
    if (!stored_remote_tar.empty())
      deployment.emplace_back("Remote Tar", stored_remote_tar);
  }

  std::vector<std::string> warnings;
  if (ssh_fields.key.empty()) {
    warnings.emplace_back("Configure SSH key: nazg server add " + label + " " +
                          host_value + " --generate-key");
  } else if (!key_present) {
    warnings.emplace_back("SSH key at " + ssh_fields.key +
                          " not found. Update with --ssh-key");
  }
  if (!ssh_target.empty())
    warnings.emplace_back("Copy key if needed: nazg bot setup --host " + ssh_target);

  std::string agent_state = map_get(*server, "agent_status", "unknown");
  bool agent_reachable = false;
  std::string agent_error;
  if (check_agent_connection(remote_host, ssh_fields.agent_port, agent_error, ectx)) {
    agent_reachable = true;
    agent_state = "online";
    ectx.store->update_server_status(to_int64(map_get(*server, "id")), "online");
    (*server)["agent_status"] = "online";
  }

  std::vector<std::pair<std::string, std::string>> agent_section = {
      {"State", agent_reachable ? "online" : agent_state},
      {"Endpoint", remote_host + ":" + std::to_string(ssh_fields.agent_port)}};

  std::vector<std::string> agent_notes;
  if (agent_reachable) {
    agent_notes.emplace_back("Agent responded successfully.");
  } else {
    agent_notes.emplace_back("Unable to contact agent (" + agent_error + ").");
    agent_notes.emplace_back("Check service: ssh " + ssh_target +
                            " 'sudo systemctl status nazg-agent --no-pager'");
    agent_notes.emplace_back("Inspect logs: ssh " + ssh_target +
                            " 'sudo journalctl -u nazg-agent --since \"5 minutes ago\"'");
    agent_notes.emplace_back("Reinstall: nazg server install-agent " + label + " [options]");
  }

  int64_t server_id = to_int64(map_get(*server, "id"));
  auto containers = ectx.store->list_containers(server_id);

  std::vector<std::string> remote_container_lines;
  if (!remote_host.empty()) {
    std::vector<RemoteContainerInfo> remote_containers;
    std::string docker_error;
    if (fetch_remote_containers(remote_host, ssh_fields.agent_port, true,
                                remote_containers, docker_error, ectx)) {
      if (remote_containers.empty()) {
        remote_container_lines.emplace_back("No containers (docker ps returned no rows).");
      } else {
        for (const auto &info : remote_containers) {
          remote_container_lines.emplace_back(info.name + "  " + info.status +
                                             "  " + info.image);
        }
      }
    } else if (!docker_error.empty()) {
      remote_container_lines.emplace_back("Unable to query docker: " + docker_error);
    }
  }
  if (remote_container_lines.empty()) {
    if (!containers.empty()) {
      for (const auto &row : containers) {
        remote_container_lines.emplace_back(map_get(row, "name") + "  " +
                                            map_get(row, "status") + "  " +
                                            map_get(row, "image"));
      }
    } else {
      remote_container_lines.emplace_back("None");
    }
  }

  std::cout << "=== Server Status: " << label << " ===" << std::endl;
  print_kv_section("Server Overview", overview);
  print_kv_section("Connectivity", connectivity);
  print_kv_section("Agent Deployment", deployment);
  print_kv_section("Agent", agent_section);
  print_bullet_section("Agent Notes", agent_notes);
  if (!warnings.empty())
    print_bullet_section("Warnings", warnings);

  print_bullet_section("Docker Containers", remote_container_lines);

  return 0;
}

int cmd_server_install_agent(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  if (ctx.argc < 4) {
    std::cerr << "Usage: nazg server install-agent <label> [--binary PATH] [--user NAME] [--remote-path PATH] [--config-path PATH]\n"
              << "       [--service-name NAME] [--port N] [--bind-address ADDR] [--controller-host HOST]\n"
              << "       [--controller-port N] [--auth-token TOKEN] [--no-systemd] [--no-start]\n"
              << "       [--use-container] [--container-runtime CMD] [--container-image IMAGE]\n"
              << "       [--container-tar PATH] [--container-remote-tar PATH]\n";
    return 1;
  }

  std::string label = ctx.argv[3];
  std::string binary_override;
  std::string remote_user_option;
  std::string remote_path = "/usr/local/bin/nazg-agent";
  std::string bind_address = "0.0.0.0";
  std::string config_path = "/etc/nazg/agent.toml";
  std::string service_name = "nazg-agent";
  bool enable_systemd = true;
  bool start_service = true;
  int port_override = -1;
  std::string controller_host_override;
  int controller_port_override = -1;
  std::string auth_token_override;
  bool reinstall = false;
  bool use_container = false;
  std::string container_runtime_override;
  std::string container_image = "ghcr.io/nazg/nazg-agent:latest";
  std::string container_tar_path;
  std::string container_remote_tar_path;
  bool container_image_flag = false;
  bool container_tar_flag = false;
  bool container_remote_tar_flag = false;

  std::string controller_host = "set-me";
  int controller_port = 7070;
  std::string auth_token = "set-me";
  std::string container_base_image = "ubuntu:22.04";

  if (ectx.cfg) {
    if (ectx.cfg->has("controlCenter", "port")) {
      controller_port = ectx.cfg->get_int("controlCenter", "port", controller_port);
    }
    if (ectx.cfg->has("controlCenter", "public_host")) {
      controller_host = ectx.cfg->get_string("controlCenter", "public_host");
    } else if (ectx.cfg->has("controlCenter", "bind_address")) {
      std::string candidate = ectx.cfg->get_string("controlCenter", "bind_address");
      if (!candidate.empty() && candidate != "0.0.0.0") {
        controller_host = candidate;
      }
    }
    if (ectx.cfg->has("controlCenter", "auth_token")) {
      auth_token = ectx.cfg->get_string("controlCenter", "auth_token");
    }
    if (ectx.cfg->has("containers", "nazg_agent_base_image")) {
      container_base_image = ectx.cfg->get_string("containers", "nazg_agent_base_image");
    }
  }

  for (int i = 4; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--binary" && i + 1 < ctx.argc) {
      binary_override = ctx.argv[++i];
    } else if (arg == "--user" && i + 1 < ctx.argc) {
      remote_user_option = ctx.argv[++i];
    } else if (arg == "--remote-path" && i + 1 < ctx.argc) {
      remote_path = ctx.argv[++i];
    } else if (arg == "--bind-address" && i + 1 < ctx.argc) {
      bind_address = ctx.argv[++i];
    } else if (arg == "--port" && i + 1 < ctx.argc) {
      port_override = static_cast<int>(to_int64(ctx.argv[++i], 0));
    } else if (arg == "--service-name" && i + 1 < ctx.argc) {
      service_name = ctx.argv[++i];
    } else if (arg == "--config-path" && i + 1 < ctx.argc) {
      config_path = ctx.argv[++i];
    } else if (arg == "--controller-host" && i + 1 < ctx.argc) {
      controller_host_override = ctx.argv[++i];
    } else if (arg == "--controller-port" && i + 1 < ctx.argc) {
      controller_port_override = static_cast<int>(to_int64(ctx.argv[++i], 0));
    } else if (arg == "--auth-token" && i + 1 < ctx.argc) {
      auth_token_override = ctx.argv[++i];
    } else if (arg == "--no-systemd") {
      enable_systemd = false;
    } else if (arg == "--no-start") {
      start_service = false;
    } else if (arg == "--reinstall") {
      reinstall = true;
    } else if (arg == "--use-container") {
      use_container = true;
    } else if (arg == "--container-runtime" && i + 1 < ctx.argc) {
      container_runtime_override = ctx.argv[++i];
    } else if (arg == "--container-image" && i + 1 < ctx.argc) {
      container_image = ctx.argv[++i];
      container_image_flag = true;
    } else if (arg == "--container-tar" && i + 1 < ctx.argc) {
      container_tar_path = ctx.argv[++i];
      container_tar_flag = true;
    } else if (arg == "--container-remote-tar" && i + 1 < ctx.argc) {
      container_remote_tar_path = ctx.argv[++i];
      container_remote_tar_flag = true;
      use_container = true;
    }
  }

  if (!controller_host_override.empty())
    controller_host = controller_host_override;
  if (controller_port_override > 0)
    controller_port = controller_port_override;
  if (!auth_token_override.empty())
    auth_token = auth_token_override;

  std::string config_container_image;
  std::string config_container_tar;
  std::string config_container_remote_tar;
  std::string config_container_runtime;
  if (ectx.cfg) {
    if (ectx.cfg->has("containers", "nazg_agent_image"))
      config_container_image = ectx.cfg->get_string("containers", "nazg_agent_image");
    if (ectx.cfg->has("containers", "nazg_agent_tar"))
      config_container_tar = ectx.cfg->get_string("containers", "nazg_agent_tar");
    if (ectx.cfg->has("containers", "nazg_agent_remote_tar"))
      config_container_remote_tar = ectx.cfg->get_string("containers", "nazg_agent_remote_tar");
    if (ectx.cfg->has("containers", "nazg_agent_runtime"))
      config_container_runtime = ectx.cfg->get_string("containers", "nazg_agent_runtime");
  }

  if (!container_image_flag && !config_container_image.empty())
    container_image = config_container_image;
  if (!container_tar_flag && !config_container_tar.empty())
    container_tar_path = config_container_tar;
  if (!container_remote_tar_flag && !config_container_remote_tar.empty())
    container_remote_tar_path = config_container_remote_tar;
  if (container_runtime_override.empty() && !config_container_runtime.empty())
    container_runtime_override = config_container_runtime;

  auto server = ectx.store->get_server(label);
  if (!server) {
    std::cerr << "Server '" << label << "' not found. Use 'nazg server list'.\n";
    return 1;
  }

  int64_t server_id = to_int64(map_get(*server, "id"));
  std::string stored_local_tar = map_get(*server, "agent_container_local_tar");
  std::string stored_remote_tar = map_get(*server, "agent_container_remote_tar");
  std::string stored_container_image = map_get(*server, "agent_container_image");
  bool stored_image_available = !stored_container_image.empty();

  if (!container_tar_flag && !stored_local_tar.empty()) {
    container_tar_path = stored_local_tar;
  }
  if (!container_remote_tar_flag && !stored_remote_tar.empty()) {
    container_remote_tar_path = stored_remote_tar;
  }
  if (!container_image_flag && !stored_container_image.empty()) {
    container_image = stored_container_image;
  }

  fs::path local_container_tar;
  bool local_tar_available = false;
  if (!container_tar_path.empty()) {
    fs::path candidate =
        fs::absolute(fs::path(::nazg::system::expand_tilde(container_tar_path)));
    if (fs::exists(candidate)) {
      local_container_tar = candidate;
      local_tar_available = true;
    } else if (container_tar_flag) {
      std::cerr << "Container tarball '" << candidate.string() << "' not found." << std::endl;
      return 1;
    } else {
      if (!stored_local_tar.empty()) {
        std::cerr << "⚠ Stored container tarball '" << candidate.string()
                  << "' not found. Falling back to defaults." << std::endl;
      }
      container_tar_path.clear();
    }
  }

  if (!local_tar_available && container_tar_path.empty() && !container_tar_flag) {
    std::vector<fs::path> defaults = {
        default_agent_tarball_path(),
        fs::current_path() / "build-self" / "nazg-agent.tar",
        fs::current_path() / "build" / "nazg-agent.tar"};
    for (const auto &candidate : defaults) {
      if (fs::exists(candidate)) {
        local_container_tar = candidate;
        container_tar_path = candidate.string();
        local_tar_available = true;
        break;
      }
    }
  }

  auto build_container_tarball = [&]() -> bool {
    if (!ectx.reg) {
      std::cerr << "Registry unavailable; cannot build agent container automatically." << std::endl;
      return false;
    }

    fs::path target = default_agent_tarball_path();
    std::error_code dir_ec;
    fs::create_directories(target.parent_path(), dir_ec);
    if (dir_ec) {
      std::cerr << "Failed to create directory for agent tarball: "
                << dir_ec.message() << std::endl;
      return false;
    }

    std::cout << "⌛ Building nazg-agent container image (no tarball found)...\n";
    std::vector<std::string> storage;
    storage.reserve(8);
    storage.push_back(ectx.prog.empty() ? "nazg" : ectx.prog);
    storage.push_back("agent");
    storage.push_back("package");
    storage.push_back("--overwrite");
    storage.push_back("--output");
    storage.push_back(target.string());

    std::vector<const char *> argv;
    argv.reserve(storage.size());
    for (auto &token : storage)
      argv.push_back(token.c_str());

    auto result = ectx.reg->dispatch("agent", ectx, argv);
    if (!result.first || result.second != 0) {
      std::cerr << "Failed to build agent tarball automatically." << std::endl;
      return false;
    }

    if (!fs::exists(target)) {
      std::cerr << "Agent tarball build completed but file not found at "
                << target << std::endl;
      return false;
    }

    local_container_tar = fs::absolute(target);
    container_tar_path = local_container_tar.string();
    local_tar_available = true;
    return true;
  };

  if (!local_tar_available && !container_tar_flag && !container_remote_tar_flag) {
    if (!build_container_tarball())
      return 1;
  }

  bool remote_tar_available = !container_remote_tar_path.empty();

  const std::string packaged_image_tag = "nazg-agent:bundle";
  if ((local_tar_available || remote_tar_available) &&
      !container_image_flag && container_image == "ghcr.io/nazg/nazg-agent:latest") {
    container_image = packaged_image_tag;
  }

  auto quote_remote_for_shell = [](const std::string &path) -> std::string {
    std::string expanded = path;
    if (expanded == "~")
      expanded = "$HOME";
    else if (expanded.rfind("~/", 0) == 0)
      expanded = std::string("$HOME/") + expanded.substr(2);
    std::string out;
    out.reserve(expanded.size() + 2);
    out.push_back('"');
    for (char ch : expanded) {
      if (ch == '"' || ch == '\\')
        out.push_back('\\');
      out.push_back(ch);
    }
    out.push_back('"');
    return out;
  };

  SshConfigFields ssh_fields = parse_ssh_config_json(map_get(*server, "ssh_config"));
  std::string ssh_key_path = ::nazg::system::expand_tilde(ssh_fields.key);
  if (ssh_key_path.empty()) {
    std::cerr << "SSH key not configured. Re-run 'nazg server add --generate-key'.\n";
    return 1;
  }

  if (!fs::exists(ssh_key_path)) {
    std::cerr << "SSH key " << ssh_fields.key << " not found. Update the server record with a valid key.\n";
    return 1;
  }

  auto agent_path_opt = find_agent_binary(binary_override);
  if (!agent_path_opt) {
    std::cerr << "Unable to locate nazg-agent binary. Build it with 'cmake --build build-self -j' or pass --binary PATH.\n";
    return 1;
  }
  fs::path agent_binary = *agent_path_opt;

  if (port_override > 0) {
    ssh_fields.agent_port = port_override;
  }

  std::string remote_address = map_get(*server, "host");
  if (remote_address.empty()) {
    std::cerr << "Server does not have a host configured. Update it with 'nazg server add'.\n";
    return 1;
  }

  std::string host_only = extract_host_only(remote_address);
  std::string remote_user = trim_copy(remote_user_option);
  auto at_pos = remote_address.find('@');
  if (remote_user.empty() && at_pos != std::string::npos && at_pos > 0) {
    remote_user = remote_address.substr(0, at_pos);
  }

  if (remote_user.empty()) {
    prompt::Prompt user_prompt(ectx.log);
    user_prompt.title("install-agent")
               .question("SSH user for " + label + " (" + host_only + ")");
    remote_user = trim_copy(user_prompt.input("root"));
    if (remote_user.empty()) {
      std::cerr << "Aborted: SSH user is required.\n";
      return 1;
    }
  }

  std::string remote_login = remote_user + "@" + host_only;

  std::cout << "Preparing to install nazg-agent on " << remote_login << "\n";
  std::cout << "Using SSH key: " << ssh_fields.key << "\n";
  std::cout << "If prompted, enter the SSH password for " << remote_login << ".\n";

  std::string scp_prefix = build_scp_prefix(ssh_fields);
  std::string ssh_prefix = build_ssh_prefix(remote_login, ssh_fields);

  std::string container_runtime = container_runtime_override;
  std::string container_runtime_path;
  bool runtime_available = false;
  auto resolve_runtime = [&](const std::string &candidate) -> bool {
    auto res = ::nazg::system::run_command_capture(
        ssh_prefix + " 'command -v " + candidate + " 2>/dev/null'");
    if (res.exit_code == 0 && !res.output.empty()) {
      container_runtime = candidate;
      container_runtime_path = trim_copy(res.output);
      runtime_available = true;
      return true;
    }
    return false;
  };

  bool has_image_source = container_image_flag || !config_container_image.empty() ||
                          stored_image_available;
  bool default_image = (!container_image_flag && config_container_image.empty() &&
                        !stored_image_available &&
                        container_image == "ghcr.io/nazg/nazg-agent:latest");
  if (default_image)
    has_image_source = false;

  bool need_runtime = use_container || !container_runtime_override.empty() ||
                      local_tar_available || remote_tar_available || has_image_source;
  if (need_runtime) {
    if (!container_runtime_override.empty()) {
      resolve_runtime(container_runtime_override);
    } else {
      if (!resolve_runtime("docker"))
        resolve_runtime("podman");
    }
  }

  if (!use_container && runtime_available &&
      ((local_tar_available || remote_tar_available) || has_image_source)) {
    use_container = true;
  }
  if (local_tar_available || remote_tar_available)
    use_container = true;

  if (use_container) {
    if (!runtime_available) {
      std::cerr << "Container runtime not found on remote host. Install docker or podman, or provide --container-runtime." << std::endl;
      return 1;
    }
    if (!(local_tar_available || remote_tar_available) && !has_image_source) {
      std::cerr << "No container image or tarball available. Provide --container-image, --container-tar, or set containers.nazg_agent_image." << std::endl;
      return 1;
    }
  }

  auto verify_result = ::nazg::system::run_command_capture(
      ssh_prefix + " 'echo nazg-agent-verify' 2>&1");
  if (verify_result.exit_code != 0) {
    std::cerr << "\nSSH key authentication failed for " << remote_login << ".\n";
    if (!verify_result.output.empty()) {
      std::cerr << verify_result.output << "\n";
    }

    prompt::Prompt fix_prompt(ectx.log);
    fix_prompt.title("install-agent")
              .question("Run 'nazg bot setup --host " + remote_login + "' now?")
              .info("Nazg needs key-based SSH to automate installs.")
              .action("Copy SSH key to " + remote_login + " and re-test")
              .warning("You will be prompted for the remote password once during setup.");

    if (fix_prompt.confirm(true)) {
      bool setup_ok = launch_bot_setup(ectx, label, remote_login, ssh_fields);
      if (setup_ok) {
        std::cout << "\nRe-testing SSH key authentication...\n";
        verify_result = ::nazg::system::run_command_capture(
            ssh_prefix + " 'echo nazg-agent-verify' 2>&1");
        if (verify_result.exit_code == 0) {
          std::cout << "SSH key authentication succeeded after bot setup.\n";
        }
      }

      if (verify_result.exit_code != 0) {
        std::cerr << "SSH key authentication still failing for " << remote_login << ".\n";
        if (!verify_result.output.empty()) {
          std::cerr << verify_result.output << "\n";
        }
        std::cerr << "Ensure the key at " << ssh_fields.key
                  << " is authorized on the remote host, then rerun install-agent.\n";
        return 1;
      }
    } else {
      std::cerr << "Nazg requires key-based SSH for automation. Run 'nazg bot setup --host "
                << remote_login << "' (or copy the key manually) and rerun install-agent.\n";
      return 1;
    }
  }

  bool needs_sudo = remote_user != "root";
  bool have_sudo_password = false;
  std::string sudo_password;

  if (needs_sudo) {
    auto sudo_check = ::nazg::system::run_command_capture(
        ssh_prefix + " 'sudo -n true' 2>&1");
    if (sudo_check.exit_code != 0) {
      prompt::Prompt sudo_prompt(ectx.log);
      sudo_prompt.title("install-agent")
                 .question("Provide sudo password for " + remote_login + "?")
                 .action("Use password to run sudo non-interactively")
                 .warning("Password is used once to complete installation.")
                 .info("Leave blank to cancel.");

      if (!sudo_prompt.confirm(true)) {
        std::cerr << "Sudo password not provided. Configure passwordless sudo or rerun with --user root." << std::endl;
        return 1;
      }

      sudo_password = read_password("Enter sudo password for " + remote_login);
      if (sudo_password.empty()) {
        std::cerr << "Aborted: sudo password is required to proceed." << std::endl;
        return 1;
      }
      have_sudo_password = true;
    }
  }

  std::string suffix = random_token(6);
  std::string remote_agent_tmp = "/tmp/nazg-agent-" + suffix + ".bin";
  std::string remote_config_tmp = "/tmp/nazg-agent-" + suffix + ".toml";
  std::string remote_service_tmp = "/tmp/nazg-agent-" + suffix + ".service";

  fs::path temp_dir = fs::temp_directory_path() / fs::path("nazg-agent-" + random_token(6));
  std::error_code ec;
  fs::create_directories(temp_dir, ec);

  if (!reinstall && !use_container) {
    auto result = ::nazg::system::run_command_capture(
        ssh_prefix + " 'command -v nazg-agent' 2>&1");
    if (result.exit_code == 0) {
      prompt::Prompt reinstall_prompt(ectx.log);
      reinstall_prompt.title("install-agent")
                      .question("nazg-agent already exists on " + remote_login + ". Reinstall it?")
                      .info("Existing binary will be replaced with the new one.")
                      .action("Uninstall and redeploy nazg-agent")
                      .warning("Service will be restarted during reinstall.");
      if (!reinstall_prompt.confirm(true)) {
        std::cout << "Aborted by user." << std::endl;
        fs::remove_all(temp_dir, ec);
        std::fill(sudo_password.begin(), sudo_password.end(), '\0');
        return 1;
      }
      reinstall = true;
    }
  }

  if (!reinstall && use_container) {
    auto unit_check = ::nazg::system::run_command_capture(
        ssh_prefix + " 'systemctl list-unit-files " + service_name + ".service --no-legend 2>/dev/null'");
    if (unit_check.exit_code == 0 && !trim_copy(unit_check.output).empty()) {
      prompt::Prompt reinstall_prompt(ectx.log);
      reinstall_prompt.title("install-agent")
                      .question(service_name + ".service already exists on " + remote_login + ". Reinstall it?")
                      .info("Existing container and unit will be replaced.")
                      .action("Remove and redeploy nazg-agent container")
                      .warning("Service will be restarted during reinstall.");
      if (!reinstall_prompt.confirm(true)) {
        std::cout << "Aborted by user." << std::endl;
        fs::remove_all(temp_dir, ec);
        std::fill(sudo_password.begin(), sudo_password.end(), '\0');
        return 1;
      }
      reinstall = true;
    }
  }

  if (reinstall) {
    std::string stop_body = "systemctl stop " + ::nazg::system::shell_quote(service_name) + " || true";
    std::string stop_exec = make_remote_command(stop_body, needs_sudo, have_sudo_password, sudo_password);
    run_local_command("stop existing agent service",
                      ssh_prefix + " " + ::nazg::system::shell_quote(stop_exec),
                      ectx,
                      ssh_prefix + " " + ::nazg::system::shell_quote(
                          make_remote_display_command(stop_body, needs_sudo, have_sudo_password, sudo_password)));

    if (use_container && !container_runtime.empty()) {
      std::string remove_container_body = container_runtime_path + " rm -f nazg-agent >/dev/null 2>&1 || true";
      std::string remove_container_exec = make_remote_command(remove_container_body, needs_sudo, have_sudo_password, sudo_password);
      run_local_command("remove existing container",
                        ssh_prefix + " " + ::nazg::system::shell_quote(remove_container_exec),
                        ectx,
                        ssh_prefix + " " + ::nazg::system::shell_quote(
                            make_remote_display_command(remove_container_body, needs_sudo, have_sudo_password, sudo_password)));
    }

    std::string remove_unit_body = "rm -f /etc/systemd/system/" + service_name + ".service";
    run_local_command("remove existing systemd unit",
                      ssh_prefix + " " + ::nazg::system::shell_quote(
                          make_remote_command(remove_unit_body, needs_sudo, have_sudo_password, sudo_password)),
                      ectx,
                      ssh_prefix + " " + ::nazg::system::shell_quote(
                          make_remote_display_command(remove_unit_body, needs_sudo, have_sudo_password, sudo_password)));

    std::string remove_bin_body = "rm -f " + ::nazg::system::shell_quote(remote_path);
    run_local_command("remove existing agent binary",
                      ssh_prefix + " " + ::nazg::system::shell_quote(
                          make_remote_command(remove_bin_body, needs_sudo, have_sudo_password, sudo_password)),
                      ectx,
                      ssh_prefix + " " + ::nazg::system::shell_quote(
                          make_remote_display_command(remove_bin_body, needs_sudo, have_sudo_password, sudo_password)));

    std::string clean_config_body = "rm -f " + ::nazg::system::shell_quote(config_path);
    run_local_command("remove existing config",
                      ssh_prefix + " " + ::nazg::system::shell_quote(
                          make_remote_command(clean_config_body, needs_sudo, have_sudo_password, sudo_password)),
                      ectx,
                      ssh_prefix + " " + ::nazg::system::shell_quote(
                          make_remote_display_command(clean_config_body, needs_sudo, have_sudo_password, sudo_password)));
  }

  fs::path local_config = temp_dir / "agent.toml";
  {
    std::ofstream config_out(local_config);
    if (!config_out) {
      std::cerr << "Failed to write temporary config file: " << local_config << "\n";
      fs::remove_all(temp_dir, ec);
      return 1;
    }
    config_out << "[agent]\n";
    config_out << "server_label = \"" << escape_json_string(label) << "\"\n";
    config_out << "control_center_host = \"" << escape_json_string(controller_host) << "\"\n";
    config_out << "control_center_port = " << controller_port << "\n";
    config_out << "auth_token = \"" << escape_json_string(auth_token) << "\"\n";
    config_out << "heartbeat_interval_sec = 30\n";
    config_out << "scan_interval_sec = 300\n\n";
    config_out << "[docker]\n";
    config_out << "enabled = true\n";
    config_out << "scan_compose_paths = [\"/opt\", \"/srv\", \"/home/*/docker\"]\n";
  }

  fs::path local_service = temp_dir / "nazg-agent.service";
  if (enable_systemd) {
    std::ofstream service_out(local_service);
    if (!service_out) {
      std::cerr << "Failed to write temporary service file: " << local_service << "\n";
      fs::remove_all(temp_dir, ec);
      return 1;
    }
    service_out << "[Unit]\n";
    service_out << "Description=Nazg Remote Agent\n";
    service_out << "After=network-online.target\n";
    service_out << "Wants=network-online.target\n\n";
    service_out << "[Service]\n";
    service_out << "Type=simple\n";
    service_out << "Restart=always\n";
    service_out << "User=root\n";
    service_out << "WorkingDirectory=/var/lib/nazg-agent\n";
    service_out << "RuntimeDirectory=nazg-agent\n";
    service_out << "RuntimeDirectoryMode=0755\n";
    if (use_container) {
      auto shell_wrap = [&](const std::string &cmd) {
        return std::string("/bin/sh -c ") +
               ::nazg::system::shell_quote("exec " + cmd);
      };

      service_out << "ExecStartPre="
                  << shell_wrap(container_runtime_path +
                               " rm -f nazg-agent >/dev/null 2>&1 || true")
                  << "\n";
      std::ostringstream run_cmd;
      run_cmd << container_runtime_path
              << " run --name nazg-agent --network host"
              << " -e NAZG_AGENT_PORT=" << ssh_fields.agent_port
              << " -e NAZG_AGENT_ADDR=" << bind_address
              << " -v /etc/nazg/agent.toml:/etc/nazg/agent.toml:ro"
              << " -v /var/run/docker.sock:/var/run/docker.sock "
              << container_image;
      service_out << "ExecStart=" << run_cmd.str() << "\n";
      service_out << "ExecStop="
                  << shell_wrap(container_runtime_path +
                               " stop nazg-agent >/dev/null 2>&1 || true")
                  << "\n";
      service_out << "ExecStopPost="
                  << shell_wrap(container_runtime_path +
                               " rm -f nazg-agent >/dev/null 2>&1 || true")
                  << "\n";
    } else {
      service_out << "Environment=NAZG_AGENT_PORT=" << ssh_fields.agent_port << "\n";
      service_out << "Environment=NAZG_AGENT_ADDR=" << bind_address << "\n";
      service_out << "ExecStart=" << remote_path << "\n";
    }
    service_out << "[Install]\n";
    service_out << "WantedBy=multi-user.target\n";
  }

  bool ok = true;
  if (!use_container) {
    std::string remote_agent_target = remote_login + ":" + remote_agent_tmp;
    ok = run_local_command(
        "copy agent binary to " + remote_agent_target,
        scp_prefix + " " + ::nazg::system::shell_quote(agent_binary.string()) + " " +
            ::nazg::system::shell_quote(remote_agent_target),
        ectx);
    if (!ok) {
      fs::remove_all(temp_dir, ec);
      return 1;
    }
  }

  std::string remote_config_target = remote_login + ":" + remote_config_tmp;
  ok = run_local_command(
      "copy agent config to " + remote_config_target,
      scp_prefix + " " + ::nazg::system::shell_quote(local_config.string()) + " " +
          ::nazg::system::shell_quote(remote_config_target),
      ectx);
  if (!ok) {
    fs::remove_all(temp_dir, ec);
    return 1;
  }

  if (use_container) {
    if (local_tar_available) {
      std::string remote_tar = "/tmp/nazg-agent-image-" + random_token(6) + ".tar";
      ok = run_local_command(
          "copy container image to " + remote_login + ":" + remote_tar,
          scp_prefix + " " + ::nazg::system::shell_quote(local_container_tar.string()) + " " +
              ::nazg::system::shell_quote(remote_login + ":" + remote_tar),
          ectx);
      if (!ok) {
        fs::remove_all(temp_dir, ec);
        return 1;
      }

      std::string load_body = container_runtime_path + " load -i " + ::nazg::system::shell_quote(remote_tar);
      std::string load_exec = make_remote_command(load_body, needs_sudo, have_sudo_password, sudo_password);
      std::string load_display = make_remote_display_command(load_body, needs_sudo, have_sudo_password, sudo_password);
      ok = run_local_command("load container image",
                             ssh_prefix + " " + ::nazg::system::shell_quote(load_exec),
                             ectx,
                             ssh_prefix + " " + ::nazg::system::shell_quote(load_display));

      std::string remove_tar_body = "rm -f " + ::nazg::system::shell_quote(remote_tar);
      std::string remove_tar_exec = make_remote_command(remove_tar_body, needs_sudo, have_sudo_password, sudo_password);
      run_local_command("remove container tar",
                        ssh_prefix + " " + ::nazg::system::shell_quote(remove_tar_exec),
                        ectx,
                        ssh_prefix + " " + ::nazg::system::shell_quote(
                            make_remote_display_command(remove_tar_body, needs_sudo, have_sudo_password, sudo_password)));

      if (!ok) {
        fs::remove_all(temp_dir, ec);
        return 1;
      }
    } else if (remote_tar_available) {
      std::string remote_tar_arg = quote_remote_for_shell(container_remote_tar_path);
      std::string check_body = "test -f " + remote_tar_arg;
      std::string check_exec = make_remote_command(check_body, needs_sudo, have_sudo_password, sudo_password);
      std::string check_display = make_remote_display_command(check_body, needs_sudo, have_sudo_password, sudo_password);
      ok = run_local_command("verify remote container tar",
                             ssh_prefix + " " + ::nazg::system::shell_quote(check_exec),
                             ectx,
                             ssh_prefix + " " + ::nazg::system::shell_quote(check_display));
      if (!ok) {
        fs::remove_all(temp_dir, ec);
        return 1;
      }

      std::string load_body = container_runtime_path + " load -i " + remote_tar_arg;
      std::string load_exec = make_remote_command(load_body, needs_sudo, have_sudo_password, sudo_password);
      std::string load_display = make_remote_display_command(load_body, needs_sudo, have_sudo_password, sudo_password);
      ok = run_local_command("load container image",
                             ssh_prefix + " " + ::nazg::system::shell_quote(load_exec),
                             ectx,
                             ssh_prefix + " " + ::nazg::system::shell_quote(load_display));
      if (!ok) {
        fs::remove_all(temp_dir, ec);
        return 1;
      }
    } else {
      std::string pull_body = container_runtime_path + " pull " + ::nazg::system::shell_quote(container_image);
      std::string pull_exec = make_remote_command(pull_body, needs_sudo, have_sudo_password, sudo_password);
      std::string pull_display = make_remote_display_command(pull_body, needs_sudo, have_sudo_password, sudo_password);
      ok = run_local_command("pull container image " + container_image,
                             ssh_prefix + " " + ::nazg::system::shell_quote(pull_exec),
                             ectx,
                             ssh_prefix + " " + ::nazg::system::shell_quote(pull_display));
      if (!ok) {
        fs::remove_all(temp_dir, ec);
        return 1;
      }
    }
  }

  if (enable_systemd) {
    ok = run_local_command(
        "copy service unit to " + remote_login + ":" + remote_service_tmp,
        scp_prefix + " " + ::nazg::system::shell_quote(local_service.string()) + " " +
            ::nazg::system::shell_quote(remote_login + ":" + remote_service_tmp),
        ectx);
    if (!ok) {
      fs::remove_all(temp_dir, ec);
      return 1;
    }
  }

  if (!use_container) {
    std::ostringstream remote_cmd;
    remote_cmd << "install -m 0755 "
               << ::nazg::system::shell_quote(remote_agent_tmp) << " "
               << ::nazg::system::shell_quote(remote_path)
               << " && rm -f " << ::nazg::system::shell_quote(remote_agent_tmp);
    std::string exec = make_remote_command(remote_cmd.str(), needs_sudo, have_sudo_password, sudo_password);
    std::string display_exec = make_remote_display_command(remote_cmd.str(), needs_sudo, have_sudo_password, sudo_password);
    ok = run_local_command("install agent binary",
                           ssh_prefix + " " + ::nazg::system::shell_quote(exec),
                           ectx,
                           ssh_prefix + " " + ::nazg::system::shell_quote(display_exec));
    if (!ok) {
      fs::remove_all(temp_dir, ec);
      return 1;
    }
  }

  {
    std::ostringstream remote_cmd;
    remote_cmd << "install -D -m 0644 "
               << ::nazg::system::shell_quote(remote_config_tmp) << " "
               << ::nazg::system::shell_quote(config_path)
               << " && rm -f " << ::nazg::system::shell_quote(remote_config_tmp);
    std::string exec = make_remote_command(remote_cmd.str(), needs_sudo, have_sudo_password, sudo_password);
    std::string display_exec = make_remote_display_command(remote_cmd.str(), needs_sudo, have_sudo_password, sudo_password);
    ok = run_local_command("install agent config",
                           ssh_prefix + " " + ::nazg::system::shell_quote(exec),
                           ectx,
                           ssh_prefix + " " + ::nazg::system::shell_quote(display_exec));
  }
  if (!ok) {
    fs::remove_all(temp_dir, ec);
    return 1;
  }

  if (enable_systemd) {
    {
      std::ostringstream remote_cmd;
      remote_cmd << "install -D -m 0644 "
                 << ::nazg::system::shell_quote(remote_service_tmp) << " "
                 << ::nazg::system::shell_quote("/etc/systemd/system/" + service_name + ".service")
                 << " && rm -f " << ::nazg::system::shell_quote(remote_service_tmp);
      std::string exec = make_remote_command(remote_cmd.str(), needs_sudo, have_sudo_password, sudo_password);
      std::string display_exec = make_remote_display_command(remote_cmd.str(), needs_sudo, have_sudo_password, sudo_password);
      ok = run_local_command("install systemd unit",
                             ssh_prefix + " " + ::nazg::system::shell_quote(exec),
                             ectx,
                             ssh_prefix + " " + ::nazg::system::shell_quote(display_exec));
    }
    if (!ok) {
      fs::remove_all(temp_dir, ec);
      return 1;
    }

    {
    std::string body = "mkdir -p /var/lib/nazg-agent && chown root:root /var/lib/nazg-agent";
      std::string exec = make_remote_command(body, needs_sudo, have_sudo_password, sudo_password);
      std::string display_exec = make_remote_display_command(body, needs_sudo, have_sudo_password, sudo_password);
      ok = run_local_command("create agent runtime directory",
                             ssh_prefix + " " + ::nazg::system::shell_quote(exec),
                             ectx,
                             ssh_prefix + " " + ::nazg::system::shell_quote(display_exec));
    }
    if (!ok) {
      fs::remove_all(temp_dir, ec);
      return 1;
    }

    std::string systemctl_body = "systemctl daemon-reload";
    if (start_service) {
      systemctl_body += " && systemctl enable --now " + ::nazg::system::shell_quote(service_name);
    }
    std::string exec = make_remote_command(systemctl_body, needs_sudo, have_sudo_password, sudo_password);
    std::string display_exec = make_remote_display_command(systemctl_body, needs_sudo, have_sudo_password, sudo_password);
    ok = run_local_command("configure systemd service",
                           ssh_prefix + " " + ::nazg::system::shell_quote(exec),
                           ectx,
                           ssh_prefix + " " + ::nazg::system::shell_quote(display_exec));
    if (!ok) {
      fs::remove_all(temp_dir, ec);
      return 1;
    }
  }

  fs::remove_all(temp_dir, ec);

  ssh_fields.key = ssh_key_path;
  std::string new_host_value = remote_login.empty() ? map_get(*server, "host")
                                                    : remote_login;
  ectx.store->update_server_connection(server_id,
                                       new_host_value,
                                       build_ssh_config_json(ssh_fields));
  ectx.store->update_server_status(server_id, "unknown");
  if (use_container) {
    std::string strategy = "image";
    std::string persist_local;
    std::string persist_remote;
    if (local_tar_available) {
      strategy = "local-tar";
      persist_local = container_tar_path;
    } else if (remote_tar_available) {
      strategy = "remote-tar";
      persist_remote = container_remote_tar_path;
    }
    ectx.store->update_server_agent_container(server_id, strategy, persist_local,
                                              persist_remote, container_image);
  } else {
    ectx.store->update_server_agent_container(server_id, "binary", "", "", "");
  }

  std::cout << "\n✓ nazg-agent installed on " << remote_login << " (listening on "
            << bind_address << ':' << ssh_fields.agent_port << ")\n";
  if (controller_host == "set-me" || auth_token == "set-me") {
    std::cout << "  Update " << config_path
              << " with your controller host/auth token before enabling phone-home mode.\n";
  }
  if (use_container) {
    std::cout << "  Container runtime: " << container_runtime
              << " (" << container_runtime_path << ")\n";
    if (local_tar_available) {
      std::cout << "  Image loaded from tar: " << local_container_tar.string() << "\n";
    } else if (remote_tar_available) {
      std::cout << "  Image loaded from remote tar: " << container_remote_tar_path << "\n";
    } else {
      std::cout << "  Image pulled: " << container_image << "\n";
    }
  }
  if (enable_systemd) {
    if (start_service) {
      std::cout << "  Service: systemctl status " << service_name << "\n";
    } else {
      std::cout << "  Service installed but not started. Run 'ssh " << remote_login
                << " sudo systemctl start " << service_name << "' when ready.\n";
    }
  } else {
    std::cout << "  Start manually: ssh " << remote_login << " '"
              << remote_path << "'\n";
  }

  std::string agent_error;
  if (check_agent_connection(host_only, ssh_fields.agent_port, agent_error, ectx)) {
    ectx.store->update_server_status(server_id, "online");
    std::cout << "  Agent responded to test requests on " << host_only << ':'
              << ssh_fields.agent_port << "\n";
  } else {
    std::cout << "  ⚠ Unable to contact agent on " << host_only << ':'
              << ssh_fields.agent_port << " (" << agent_error << ")\n"
              << "    Check: ssh " << remote_login
              << " 'sudo systemctl status " << service_name << " --no-pager'\n"
              << "    Inspect logs: ssh " << remote_login
              << " 'sudo journalctl -u " << service_name << " --since \"5 minutes ago\"'\n";

    std::string status_body = "systemctl status " + ::nazg::system::shell_quote(service_name) + " --no-pager";
    std::string status_exec = make_remote_command(status_body, needs_sudo, have_sudo_password, sudo_password);
    auto status_result = ::nazg::system::run_command_capture(
        ssh_prefix + " " + ::nazg::system::shell_quote(status_exec));
    if (!status_result.output.empty()) {
      std::cout << "\n--- nazg-agent systemd status ---\n" << status_result.output
                << "\n---------------------------------\n";
      if (status_result.output.find("GLIBC") != std::string::npos ||
          status_result.output.find("GLIBCXX") != std::string::npos) {
        std::cout << "    ⚠ The remote system reports missing GLIBC/GLIBCXX symbols.\n"
                  << "      Build nazg-agent on that host (or inside a container matching its distro)\n"
                  << "      so it links against local libraries, or install compatible runtime libraries.\n"
                  << "      Alternative: nazg server install-agent " << label
                  << " --use-container [--container-image IMAGE].\n";
      }
    }
  }

  std::cout << "\nVerify with: ./build-self/nazg docker list --server " << label << "\n";
  std::fill(sudo_password.begin(), sudo_password.end(), '\0');
  return 0;
}

int cmd_server(const command_context &ctx, const context &ectx) {
  if (ctx.argc < 3) {
    print_server_help();
    return 1;
  }

  std::string sub = ctx.argv[2];
  if (sub == "add")
    return cmd_server_add(ctx, ectx);
  if (sub == "list")
    return cmd_server_list(ctx, ectx);
  if (sub == "status")
    return cmd_server_status(ctx, ectx);
  if (sub == "install-agent")
    return cmd_server_install_agent(ctx, ectx);

  std::cerr << "Unknown server subcommand: " << sub << "\n";
  print_server_help();
  return 1;
}

void print_docker_help() {
  std::cout << "Usage: nazg docker <command> [options]\n"
            << "Commands:\n"
            << "  list [--server LABEL] [--all]\n"
            << "  images --server LABEL [--all] [--dangling]\n"
            << "  rm <container>... --server LABEL [--force] [--volumes]\n"
            << "  prune-images --server LABEL [--all]\n"
            << "  status <container> --server LABEL\n"
            << "  history <container> --server LABEL [--limit N]\n"
            << "  events [--server LABEL] [--limit N] [--follow]\n"
            << "  run <image> [--server LABEL] [--pull] [--detach] [--rm|--no-rm] [--name NAME] [-- ... docker args ...]\n";
}

int cmd_docker_run(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  std::string server_label;
  std::string image;
  bool pull_image = false;
  bool detach = false;
  bool rm_explicit = false;
  bool auto_rm = false;
  std::vector<std::string> run_tokens;

  for (int i = 3; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--server" && i + 1 < ctx.argc) {
      server_label = ctx.argv[++i];
    } else if (arg == "--pull") {
      pull_image = true;
    } else if (arg == "--detach" || arg == "-d") {
      detach = true;
    } else if (arg == "--rm") {
      auto_rm = true;
      rm_explicit = true;
    } else if (arg == "--no-rm") {
      auto_rm = false;
      rm_explicit = true;
    } else if ((arg == "--name" || arg == "-n") && i + 1 < ctx.argc) {
      run_tokens.push_back(arg);
      run_tokens.push_back(ctx.argv[++i]);
    } else if (arg == "--" ) {
      for (++i; i < ctx.argc; ++i) {
        run_tokens.push_back(ctx.argv[i]);
      }
      break;
    } else if (image.empty() && !arg.empty() && arg[0] != '-') {
      image = arg;
    } else {
      run_tokens.push_back(arg);
    }
  }

  if (image.empty()) {
    std::cerr << "Usage: nazg docker run <image> --server LABEL [options]\n";
    return 1;
  }

  if (server_label.empty()) {
    std::cerr << "Error: --server is required\n";
    return 1;
  }

  if (detach && !rm_explicit) {
    auto_rm = false;
  }

  if (auto_rm) {
    run_tokens.insert(run_tokens.begin(), "--rm");
  }

  auto server = ectx.store->get_server(server_label);
  if (!server) {
    std::cerr << "Server '" << server_label << "' not found. Use 'nazg server list'.\n";
    return 1;
  }

  int64_t server_id = to_int64(map_get(*server, "id"));
  SshConfigFields ssh_fields = parse_ssh_config_json(map_get(*server, "ssh_config"));
  int agent_port = ssh_fields.agent_port > 0 ? ssh_fields.agent_port : 7070;
  std::string remote_host = extract_host_only(map_get(*server, "host"));

  if (remote_host.empty()) {
    std::cerr << "Error: Server does not have a host configured. Update it with 'nazg server add --generate-key'.\n";
    return 1;
  }

  std::ostringstream script;
  script << "#!/usr/bin/env bash\n";
  script << "set -euo pipefail\n";
  script << "export PATH=$PATH:/usr/local/bin\n";
  if (pull_image) {
    script << "docker pull " << ::nazg::system::shell_quote(image) << "\n";
  }

  std::ostringstream run_command;
  run_command << "docker run";
  for (const auto &token : run_tokens) {
    run_command << ' ' << ::nazg::system::shell_quote(token);
  }
  run_command << ' ' << ::nazg::system::shell_quote(image);

  script << run_command.str() << "\n";

  std::string stdout_output;
  std::string agent_error;
  int exit_code = -1;

  std::ostringstream params_json;
  params_json << "{\"image\":\"" << escape_json_string(image) << "\"";
  params_json << ",\"args\":[";
  for (std::size_t i = 0; i < run_tokens.size(); ++i) {
    if (i != 0)
      params_json << ',';
    params_json << "\"" << escape_json_string(run_tokens[i]) << "\"";
  }
  params_json << "]";
  params_json << ",\"pull\":" << (pull_image ? "true" : "false");
  params_json << ",\"detach\":" << (detach ? "true" : "false");
  params_json << "}";

  std::string issued_by = ectx.prog.empty() ? "cli" : ectx.prog;
  int64_t command_id = ectx.store->add_docker_command(server_id, "", "run",
                                                      params_json.str(), issued_by);

  bool ok = run_agent_script(remote_host, agent_port, script.str(), exit_code,
                             stdout_output, agent_error, ectx);

  if (!ok) {
    if (command_id > 0) {
      ectx.store->update_docker_command_status(command_id, "failed", -1,
                                               "", agent_error);
    }
    std::cerr << "Failed to execute docker command via agent: " << agent_error << "\n";
    std::cerr << "Ensure nazg-agent is running on " << remote_host << " (port "
              << agent_port << ") and that the agent can access the Docker daemon.\n";
    std::cerr << "Hint: copy build-self/nazg-agent to the server and start it with --config /etc/nazg/agent.toml.\n";
    return 1;
  }

  if (command_id > 0) {
    ectx.store->update_docker_command_status(command_id,
                                             exit_code == 0 ? "success" : "failed",
                                             exit_code, stdout_output,
                                             exit_code == 0 ? "" : "docker run returned non-zero");
  }

  if (!stdout_output.empty()) {
    std::cout << stdout_output;
    if (!stdout_output.empty() && stdout_output.back() != '\n') {
      std::cout << '\n';
    }
  }

  if (exit_code == 0) {
    std::cout << "\n✓ docker run completed successfully (exit code 0)\n";
    return 0;
  }

  std::cerr << "\nCommand exited with code " << exit_code << ".\n";
  return 1;
}

int cmd_docker_list(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  std::string server_label;
  bool show_all = false;
  for (int i = 3; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if ((arg == "--server" || arg == "-s") && i + 1 < ctx.argc) {
      server_label = ctx.argv[++i];
    } else if (arg == "--all" || arg == "-a") {
      show_all = true;
    }
  }

  if (!server_label.empty()) {
    auto id = resolve_server_id(ectx.store, server_label);
    if (!id) {
      std::cerr << "Server '" << server_label << "' not found. Use 'nazg server list'.\n";
      return 1;
    }

    auto server = ectx.store->get_server(server_label);
    if (server) {
      SshConfigFields ssh_fields = parse_ssh_config_json(map_get(*server, "ssh_config"));
      std::string remote_host = extract_host_only(map_get(*server, "host"));
      if (!remote_host.empty()) {
        std::vector<RemoteContainerInfo> remote_containers;
        std::string docker_error;
        if (fetch_remote_containers(remote_host, ssh_fields.agent_port, show_all,
                                    remote_containers, docker_error, ectx)) {
          std::cout << std::left << std::setw(24) << "NAME"
                    << std::setw(26) << "STATUS"
                    << "IMAGE" << '\n';
          std::cout << std::string(24 + 26 + 40, '-') << '\n';
          if (remote_containers.empty()) {
            std::cout << "(no containers)" << std::endl;
          } else {
            for (const auto &info : remote_containers) {
              std::cout << std::left << std::setw(24) << info.name
                        << std::setw(26) << info.status
                        << info.image << '\n';
            }
          }
          return 0;
        }
        if (!docker_error.empty()) {
          std::cerr << "Unable to query docker on '" << server_label
                    << "': " << docker_error << "\n";
        }
      }
    }

    auto containers = ectx.store->list_containers(*id);
    auto labels = build_server_label_map(ectx.store);
    labels[*id] = server_label;
    print_container_table(containers, labels);
    return 0;
  }

  auto containers = ectx.store->list_containers(0);
  auto labels = build_server_label_map(ectx.store);
  print_container_table(containers, labels);
  return 0;
}

int cmd_docker_images(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  std::string server_label;
  bool show_all = false;
  bool dangling_only = false;
  for (int i = 3; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if ((arg == "--server" || arg == "-s") && i + 1 < ctx.argc) {
      server_label = ctx.argv[++i];
    } else if (arg == "--all" || arg == "-a") {
      show_all = true;
    } else if (arg == "--dangling" || arg == "-d") {
      dangling_only = true;
    }
  }

  if (server_label.empty()) {
    std::cerr << "Error: --server is required." << std::endl;
    return 1;
  }

  auto server_id = resolve_server_id(ectx.store, server_label);
  if (!server_id) {
    std::cerr << "Server '" << server_label << "' not found." << std::endl;
    return 1;
  }

  auto server = ectx.store->get_server(server_label);
  if (!server) {
    std::cerr << "Server metadata unavailable." << std::endl;
    return 1;
  }

  SshConfigFields ssh_fields = parse_ssh_config_json(map_get(*server, "ssh_config"));
  std::string remote_host = extract_host_only(map_get(*server, "host"));
  if (remote_host.empty()) {
    std::cerr << "Server host not configured." << std::endl;
    return 1;
  }

  std::ostringstream script;
  script << "#!/usr/bin/env bash\n"
         << "set -euo pipefail\n"
         << "PATH=$PATH:/usr/local/bin\n"
         << "docker images";
  if (show_all)
    script << " -a";
  if (dangling_only)
    script << " --filter dangling=true";
  script << " --format '{{.Repository}}\t{{.Tag}}\t{{.ID}}\t{{.Size}}\t{{.CreatedSince}}'\n";

  int exit_code = -1;
  std::string stdout_output;
  std::string agent_error;
  bool ok = run_agent_script(remote_host, ssh_fields.agent_port, script.str(), exit_code,
                             stdout_output, agent_error, ectx);
  if (!ok || exit_code != 0) {
    std::cerr << "Failed to query docker images on '" << server_label << "'." << std::endl;
    std::cerr << (agent_error.empty() ? stdout_output : agent_error) << std::endl;
    return 1;
  }

  std::cout << std::left << std::setw(28) << "REPOSITORY"
            << std::setw(16) << "TAG"
            << std::setw(16) << "IMAGE ID"
            << std::setw(12) << "SIZE"
            << "CREATED" << '\n';
  std::cout << std::string(28 + 16 + 16 + 12 + 12, '-') << '\n';

  std::istringstream iss(stdout_output);
  std::string line;
  bool any = false;
  while (std::getline(iss, line)) {
    if (line.empty())
      continue;
    std::istringstream row(line);
    std::string repo, tag, id, size, created;
    std::getline(row, repo, '\t');
    std::getline(row, tag, '\t');
    std::getline(row, id, '\t');
    std::getline(row, size, '\t');
    std::getline(row, created, '\t');
    std::cout << std::left << std::setw(28) << repo
              << std::setw(16) << tag
              << std::setw(16) << id
              << std::setw(12) << size
              << created << '\n';
    any = true;
  }

  if (!any)
    std::cout << "(no images)" << std::endl;

  return 0;
}

int cmd_docker_rm(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  std::string server_label;
  bool force = false;
  bool volumes = false;
  std::vector<std::string> targets;
  bool capture = false;

  for (int i = 3; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (capture || arg == "--") {
      capture = true;
      if (arg != "--")
        targets.push_back(arg);
      continue;
    }
    if ((arg == "--server" || arg == "-s") && i + 1 < ctx.argc) {
      server_label = ctx.argv[++i];
    } else if (arg == "--force" || arg == "-f") {
      force = true;
    } else if (arg == "--volumes" || arg == "-v") {
      volumes = true;
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option for docker rm: " << arg << "\n";
      return 1;
    } else {
      targets.push_back(arg);
    }
  }

  if (targets.empty()) {
    std::cerr << "Usage: nazg docker rm <container>... --server LABEL [--force] [--volumes]\n";
    return 1;
  }

  if (server_label.empty()) {
    std::cerr << "Error: --server is required." << std::endl;
    return 1;
  }

  auto server_id = resolve_server_id(ectx.store, server_label);
  if (!server_id) {
    std::cerr << "Server '" << server_label << "' not found." << std::endl;
    return 1;
  }

  auto server = ectx.store->get_server(server_label);
  if (!server) {
    std::cerr << "Server metadata unavailable." << std::endl;
    return 1;
  }

  SshConfigFields ssh_fields = parse_ssh_config_json(map_get(*server, "ssh_config"));
  std::string remote_host = extract_host_only(map_get(*server, "host"));
  if (remote_host.empty()) {
    std::cerr << "Server host not configured." << std::endl;
    return 1;
  }

  std::ostringstream script;
  script << "#!/usr/bin/env bash\n"
         << "set -euo pipefail\n"
         << "PATH=$PATH:/usr/local/bin\n"
         << "docker rm";
  if (force)
    script << " -f";
  if (volumes)
    script << " -v";
  for (const auto &name : targets)
    script << ' ' << ::nazg::system::shell_quote(name);
  script << "\n";

  int exit_code = -1;
  std::string stdout_output;
  std::string agent_error;
  bool ok = run_agent_script(remote_host, ssh_fields.agent_port, script.str(), exit_code,
                             stdout_output, agent_error, ectx);
  if (!ok || exit_code != 0) {
    std::cerr << "Failed to remove containers on '" << server_label << "'." << std::endl;
    std::cerr << (agent_error.empty() ? stdout_output : agent_error) << std::endl;
    return 1;
  }

  if (!stdout_output.empty())
    std::cout << stdout_output;
  std::cout << "\n✓ docker rm completed" << std::endl;
  return 0;
}

int cmd_docker_prune_images(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  std::string server_label;
  bool all_images = false;
  for (int i = 3; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if ((arg == "--server" || arg == "-s") && i + 1 < ctx.argc) {
      server_label = ctx.argv[++i];
    } else if (arg == "--all" || arg == "-a") {
      all_images = true;
    }
  }

  if (server_label.empty()) {
    std::cerr << "Error: --server is required." << std::endl;
    return 1;
  }

  auto server_id = resolve_server_id(ectx.store, server_label);
  if (!server_id) {
    std::cerr << "Server '" << server_label << "' not found." << std::endl;
    return 1;
  }

  auto server = ectx.store->get_server(server_label);
  if (!server) {
    std::cerr << "Server metadata unavailable." << std::endl;
    return 1;
  }

  SshConfigFields ssh_fields = parse_ssh_config_json(map_get(*server, "ssh_config"));
  std::string remote_host = extract_host_only(map_get(*server, "host"));
  if (remote_host.empty()) {
    std::cerr << "Server host not configured." << std::endl;
    return 1;
  }

  std::ostringstream script;
  script << "#!/usr/bin/env bash\n"
         << "set -euo pipefail\n"
         << "PATH=$PATH:/usr/local/bin\n"
         << "docker image prune -f";
  if (all_images)
    script << " -a";
  script << "\n";

  int exit_code = -1;
  std::string stdout_output;
  std::string agent_error;
  bool ok = run_agent_script(remote_host, ssh_fields.agent_port, script.str(), exit_code,
                             stdout_output, agent_error, ectx);
  if (!ok || exit_code != 0) {
    std::cerr << "Failed to prune images on '" << server_label << "'." << std::endl;
    std::cerr << (agent_error.empty() ? stdout_output : agent_error) << std::endl;
    return 1;
  }

  if (!stdout_output.empty())
    std::cout << stdout_output;
  std::cout << "\n✓ docker image prune completed" << std::endl;
  return 0;
}

int cmd_docker_status(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  if (ctx.argc < 4) {
    std::cerr << "Usage: nazg docker status <container> --server LABEL\n";
    return 1;
  }

  std::string container = ctx.argv[3];
  std::string server_label;
  for (int i = 4; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if ((arg == "--server" || arg == "-s") && i + 1 < ctx.argc) {
      server_label = ctx.argv[++i];
    }
  }

  if (server_label.empty()) {
    std::cerr << "Error: --server is required." << std::endl;
    return 1;
  }

  auto server_id = resolve_server_id(ectx.store, server_label);
  if (!server_id) {
    std::cerr << "Server '" << server_label << "' not found." << std::endl;
    return 1;
  }

  auto container_info = ectx.store->get_container(*server_id, container);
  if (!container_info) {
    std::cerr << "Container '" << container << "' not found for server '" << server_label << "'.\n";
    return 1;
  }

  std::cout << "Container: " << map_get(*container_info, "name") << "\n";
  std::cout << "  Server       : " << server_label << "\n";
  std::cout << "  State        : " << map_get(*container_info, "state") << "\n";
  std::cout << "  Status       : " << map_get(*container_info, "status") << "\n";
  std::cout << "  Image        : " << map_get(*container_info, "image") << "\n";
  std::cout << "  Health       : " << map_get(*container_info, "health_status") << "\n";
  std::cout << "  Restart      : " << map_get(*container_info, "restart_policy") << "\n";
  std::cout << "  Created      : " << format_timestamp(map_get(*container_info, "created")) << "\n";
  std::cout << "  Last Seen    : " << format_timestamp(map_get(*container_info, "last_seen")) << "\n";
  std::cout << "  Networks     : " << map_get(*container_info, "networks") << "\n";
  std::cout << "  Ports        : " << map_get(*container_info, "ports") << "\n";
  std::cout << "  Volumes      : " << map_get(*container_info, "volumes") << "\n";
  std::cout << "  Labels       : " << map_get(*container_info, "labels") << "\n";
  std::cout << "  Depends On   : " << map_get(*container_info, "depends_on") << "\n";

  return 0;
}

int cmd_docker_history(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  if (ctx.argc < 4) {
    std::cerr << "Usage: nazg docker history <container> --server LABEL [--limit N]\n";
    return 1;
  }

  std::string container = ctx.argv[3];
  std::string server_label;
  int limit = 50;
  for (int i = 4; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if ((arg == "--server" || arg == "-s") && i + 1 < ctx.argc) {
      server_label = ctx.argv[++i];
    } else if ((arg == "--limit" || arg == "-n") && i + 1 < ctx.argc) {
      limit = static_cast<int>(to_int64(ctx.argv[++i], limit));
    }
  }

  if (server_label.empty()) {
    std::cerr << "Error: --server is required." << std::endl;
    return 1;
  }

  auto server_id = resolve_server_id(ectx.store, server_label);
  if (!server_id) {
    std::cerr << "Server '" << server_label << "' not found." << std::endl;
    return 1;
  }

  auto events = ectx.store->get_container_history(*server_id, container, limit);
  if (events.empty()) {
    std::cout << "No history recorded for container '" << container << "'.\n";
    return 0;
  }

  std::map<int64_t, std::string> labels;
  labels[*server_id] = server_label;
  print_events(events, labels);
  return 0;
}

int cmd_docker_events(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  std::string server_label;
  int limit = 50;
  bool follow = false;

  for (int i = 3; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if ((arg == "--server" || arg == "-s") && i + 1 < ctx.argc) {
      server_label = ctx.argv[++i];
    } else if ((arg == "--limit" || arg == "-n") && i + 1 < ctx.argc) {
      limit = static_cast<int>(to_int64(ctx.argv[++i], limit));
    } else if (arg == "--follow" || arg == "-f") {
      follow = true;
    }
  }

  if (limit <= 0)
    limit = 50;

  int64_t server_id = 0;
  if (!server_label.empty()) {
    auto id = resolve_server_id(ectx.store, server_label);
    if (!id) {
      std::cerr << "Server '" << server_label << "' not found." << std::endl;
      return 1;
    }
    server_id = *id;
  }

  auto labels = build_server_label_map(ectx.store);
  if (!server_label.empty())
    labels[server_id] = server_label;

  int64_t last_id = 0;
  auto print_batch = [&](const std::vector<std::map<std::string, std::string>> &rows) {
    if (rows.empty())
      return;
    std::vector<const std::map<std::string, std::string> *> ordered;
    ordered.reserve(rows.size());
    for (const auto &row : rows)
      ordered.push_back(&row);
    std::sort(ordered.begin(), ordered.end(), [](auto *lhs, auto *rhs) {
      return to_int64(map_get(*lhs, "timestamp")) < to_int64(map_get(*rhs, "timestamp"));
    });

    bool header_printed = false;
    for (auto *ptr : ordered) {
      int64_t id = to_int64(map_get(*ptr, "id"));
      if (follow && id <= last_id)
        continue;
      if (!header_printed) {
        print_event_header();
        header_printed = true;
      }
      int64_t sid = to_int64(map_get(*ptr, "server_id"));
      std::string server = labels.count(sid) ? labels.at(sid) : std::to_string(sid);
      std::cout << std::setw(6) << map_get(*ptr, "id")
                << std::setw(16) << server
                << std::setw(24) << format_timestamp(map_get(*ptr, "timestamp"))
                << std::setw(22) << map_get(*ptr, "container_name")
                << std::setw(14) << map_get(*ptr, "event_type")
                << std::setw(16) << map_get(*ptr, "old_state")
                << std::setw(16) << map_get(*ptr, "new_state")
                << map_get(*ptr, "metadata") << "\n";
      last_id = std::max(last_id, id);
    }
    if (header_printed)
      std::cout.flush();
  };

  auto rows = ectx.store->get_recent_docker_events(server_id, limit);
  if (rows.empty() && !follow) {
    std::cout << "No docker events recorded." << std::endl;
    return 0;
  }
  print_batch(rows);

  if (!follow)
    return 0;

  if (rows.empty())
    std::cout << "Waiting for events..." << std::endl;

  if (ectx.log)
    ectx.log->info("Docker", "Following docker events (Ctrl+C to stop)");

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    auto batch = ectx.store->get_recent_docker_events(server_id, limit);
    print_batch(batch);
  }

  return 0;
}

int cmd_docker(const command_context &ctx, const context &ectx) {
  if (ctx.argc < 3) {
    print_docker_help();
    return 1;
  }

  std::string sub = ctx.argv[2];
  if (sub == "list")
    return cmd_docker_list(ctx, ectx);
  if (sub == "status")
    return cmd_docker_status(ctx, ectx);
  if (sub == "history")
    return cmd_docker_history(ctx, ectx);
  if (sub == "events")
    return cmd_docker_events(ctx, ectx);
  if (sub == "run")
    return cmd_docker_run(ctx, ectx);
  if (sub == "images")
    return cmd_docker_images(ctx, ectx);
  if (sub == "rm")
    return cmd_docker_rm(ctx, ectx);
  if (sub == "prune-images")
    return cmd_docker_prune_images(ctx, ectx);

  std::cerr << "Unknown docker subcommand: " << sub << "\n";
  print_docker_help();
  return 1;
}

} // namespace

void register_docker_commands(registry &reg, const context &ctx) {
  (void)ctx;
  command_spec server_spec{};
  server_spec.name = "server";
  server_spec.summary = "Manage remote servers monitored by nazg";
  server_spec.run = &cmd_server;
  reg.add(server_spec);

  command_spec docker_spec{};
  docker_spec.name = "docker";
  docker_spec.summary = "Inspect docker containers and events from monitored servers";
  docker_spec.run = &cmd_docker;
  reg.add(docker_spec);
}

} // namespace nazg::directive

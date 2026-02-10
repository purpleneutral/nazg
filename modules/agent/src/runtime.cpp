#include "agent/runtime.hpp"
#include "agent/protocol.hpp"
#include "agent/docker_scanner.hpp"
#include "agent/local_store.hpp"
#include "blackbox/logger.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace nazg::agent {

Runtime::Runtime(const Options &opts, ::nazg::blackbox::logger *log)
    : opts_(opts), log_(log) {
  if (opts_.enable_docker_monitoring) {
    scanner_ = std::make_unique<DockerScanner>(log_);
  }
}

Runtime::~Runtime() { stop(); }

bool Runtime::start() {
  if (running_.load()) {
    return true;
  }

  port_in_use_ = opts_.port;

  // Initialize database if Docker monitoring is enabled
  if (opts_.enable_docker_monitoring && !init_database()) {
    if (log_) {
      log_->error("agent", "Failed to initialize database");
    }
    return false;
  }

  if (!bind_socket()) {
    port_in_use_ = 0;
    return false;
  }

  running_.store(true);
  thread_ = std::make_unique<std::thread>(&Runtime::accept_loop, this);

  // Start scanner thread if Docker monitoring is enabled
  if (opts_.enable_docker_monitoring) {
    scanner_thread_ = std::make_unique<std::thread>(&Runtime::scanner_loop, this);
  }

  if (log_) {
    log_->info("agent", "Agent runtime started on " + opts_.bind_address + ":" + std::to_string(opts_.port));
    if (opts_.enable_docker_monitoring) {
      log_->info("agent", "Docker monitoring enabled (scan interval: " +
                 std::to_string(opts_.scan_interval_seconds) + "s)");
    }
  }
  return true;
}

void Runtime::stop() {
  if (!running_.exchange(false)) {
    return;
  }

  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }

  port_in_use_ = 0;

  if (thread_ && thread_->joinable()) {
    thread_->join();
  }

  if (scanner_thread_ && scanner_thread_->joinable()) {
    scanner_thread_->join();
  }

  if (log_) {
    log_->info("agent", "Agent runtime stopped");
  }
}

bool Runtime::bind_socket() {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    if (log_) {
      log_->error("agent", "Failed to create socket: " + std::string(std::strerror(errno)));
    }
    return false;
  }

  int opt = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(opts_.port);
  if (::inet_pton(AF_INET, opts_.bind_address.c_str(), &addr.sin_addr) <= 0) {
    if (log_) {
      log_->error("agent", "Invalid bind address: " + opts_.bind_address);
    }
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    if (log_) {
      log_->error("agent", "Failed to bind socket: " + std::string(std::strerror(errno)));
    }
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (::listen(listen_fd_, 8) < 0) {
    if (log_) {
      log_->error("agent", "Failed to listen on socket: " + std::string(std::strerror(errno)));
    }
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  sockaddr_in actual{};
  socklen_t actual_len = sizeof(actual);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&actual), &actual_len) == 0) {
    port_in_use_ = ntohs(actual.sin_port);
  }

  return true;
}

void Runtime::accept_loop() {
  while (running_.load()) {
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr), &len);
    if (client_fd < 0) {
      if (!running_.load()) {
        break;
      }
      if (log_) {
        log_->warn("agent", "Accept failed: " + std::string(std::strerror(errno)));
      }
      continue;
    }

    if (log_) {
      log_->info("agent", "Accepted controller connection");
    }

    handle_client(client_fd);
    ::close(client_fd);
  }
}

void Runtime::handle_client(int client_fd) {
  using namespace nazg::agent::protocol;

  Header header{};
  std::string payload;
  if (!read_message(client_fd, header, payload)) {
    return;
  }

  if (header.type != MessageType::Hello) {
    if (log_) {
      log_->warn("agent", "Unexpected initial message type");
    }
    return;
  }

  if (log_) {
    log_->info("agent", "Received hello from controller: " + payload);
  }

  auto send_packet = [&](MessageType type, const std::string &body) {
    auto packet = encode(Header{type, 0}, body);
    ::send(client_fd, packet.data(), packet.size(), 0);
  };

  send_packet(MessageType::HelloAck, "nazg-agent/0.1");

  while (read_message(client_fd, header, payload)) {
    switch (header.type) {
    case MessageType::RunCommand: {
      auto [exit_code, output] = run_script(payload);
      if (log_) {
        log_->info("agent", "Executed command, exit=" + std::to_string(exit_code));
      }
      std::string body = std::to_string(exit_code) + "\n" + output;
      send_packet(MessageType::Result, body);
      break;
    }
    case MessageType::DockerFullScan: {
      if (log_) {
        log_->info("agent", "Received request for full Docker scan");
      }
      if (scanner_ && opts_.enable_docker_monitoring) {
        std::string scan_json = scanner_->generate_full_scan_json(opts_.server_label);
        send_packet(MessageType::DockerFullScan, scan_json);
        if (log_) {
          log_->info("agent", "Sent full Docker scan (" + std::to_string(scan_json.size()) + " bytes)");
        }
      } else {
        send_packet(MessageType::Error, "Docker monitoring not enabled");
      }
      break;
    }
    case MessageType::Register: {
      // Handle registration request from control center
      if (log_) {
        log_->info("agent", "Received registration request");
      }
      std::string reg_response = build_registration_response();
      send_packet(MessageType::RegisterAck, reg_response);
      break;
    }
    case MessageType::Heartbeat:
      send_packet(MessageType::Heartbeat, "ok");
      break;
    case MessageType::Hello:
      send_packet(MessageType::HelloAck, "nazg-agent/0.1");
      break;
    default:
      if (log_) {
        log_->warn("agent", "Unsupported message type received");
      }
      send_packet(MessageType::Error, "unsupported message");
      break;
    }
  }
}

bool Runtime::read_message(int fd, protocol::Header &header, std::string &payload) {
  using namespace nazg::agent::protocol;

  Header tmp{};
  std::size_t header_bytes = 0;
  unsigned char *hdr_buf = reinterpret_cast<unsigned char *>(&tmp);
  while (header_bytes < sizeof(Header)) {
    ssize_t received = ::recv(fd, hdr_buf + header_bytes, sizeof(Header) - header_bytes, 0);
    if (received <= 0) {
      return false;
    }
    header_bytes += static_cast<std::size_t>(received);
  }

  header = tmp;

  payload.clear();
  if (header.payload_size == 0) {
    return true;
  }

  payload.resize(header.payload_size);
  std::size_t offset = 0;
  while (offset < header.payload_size) {
    ssize_t received = ::recv(fd, payload.data() + offset, header.payload_size - offset, 0);
    if (received <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(received);
  }

  return true;
}

std::pair<int, std::string> Runtime::run_script(const std::string &script) {
  char tmp_path[] = "/tmp/nazg-agent-scriptXXXXXX";
  int fd = ::mkstemp(tmp_path);
  if (fd < 0) {
    return {-1, std::string("mkstemp failed: ") + std::strerror(errno)};
  }

  ssize_t written = ::write(fd, script.data(), script.size());
  ::close(fd);
  if (written < 0 || static_cast<std::size_t>(written) != script.size()) {
    ::unlink(tmp_path);
    return {-1, std::string("write failed: ") + std::strerror(errno)};
  }

  ::chmod(tmp_path, 0700);

  std::string command = std::string("bash ") + tmp_path + " 2>&1";
  FILE *pipe = ::popen(command.c_str(), "r");
  if (!pipe) {
    ::unlink(tmp_path);
    return {-1, std::string("popen failed: ") + std::strerror(errno)};
  }

  std::string output;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }

  int status = ::pclose(pipe);
  ::unlink(tmp_path);

  int exit_code = -1;
  if (WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
  }

  return {exit_code, output};
}

bool Runtime::init_database() {
  // Ensure database directory exists
  std::string db_dir = opts_.db_path.substr(0, opts_.db_path.find_last_of("/\\"));
  std::filesystem::path dir_path(db_dir);

  try {
    if (!std::filesystem::exists(dir_path)) {
      std::filesystem::create_directories(dir_path);
      if (log_) {
        log_->info("agent", "Created database directory: " + db_dir);
      }
    }
  } catch (const std::exception &e) {
    if (log_) {
      log_->error("agent", "Failed to create database directory: " + std::string(e.what()));
    }
    return false;
  }

  // Initialize local store
  store_ = LocalStore::create(opts_.db_path, log_);
  if (!store_) {
    if (log_) {
      log_->error("agent", "Failed to create local store");
    }
    return false;
  }

  if (!store_->initialize()) {
    if (log_) {
      log_->error("agent", "Failed to initialize local store");
    }
    return false;
  }

  // Set server label
  store_->set_server_label(opts_.server_label);

  if (log_) {
    log_->info("agent", "Database initialized at " + opts_.db_path);
  }

  return true;
}

void Runtime::scanner_loop() {
  if (log_) {
    log_->info("agent", "Docker scanner loop started");
  }

  while (running_.load()) {
    // Perform scan
    try {
      perform_docker_scan();
    } catch (const std::exception &e) {
      if (log_) {
        log_->error("agent", "Docker scan failed: " + std::string(e.what()));
      }
    }

    // Sleep for the configured interval
    for (int i = 0; i < opts_.scan_interval_seconds && running_.load(); i++) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  if (log_) {
    log_->info("agent", "Docker scanner loop stopped");
  }
}

void Runtime::perform_docker_scan() {
  if (!scanner_ || !store_) {
    return;
  }

  auto now = std::chrono::system_clock::now();
  auto scan_time = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  if (log_) {
    log_->info("agent", "Performing Docker scan...");
  }

  // Check if Docker is available
  if (!scanner_->is_docker_available()) {
    if (log_) {
      log_->warn("agent", "Docker is not available, skipping scan");
    }
    return;
  }

  // Scan containers
  auto containers = scanner_->list_containers();
  if (log_) {
    log_->info("agent", "Found " + std::to_string(containers.size()) + " containers");
  }
  store_->store_containers(containers, scan_time);

  // Scan images
  auto images = scanner_->list_images();
  if (log_) {
    log_->info("agent", "Found " + std::to_string(images.size()) + " images");
  }
  store_->store_images(images, scan_time);

  // Scan networks
  auto networks = scanner_->list_networks();
  store_->store_networks(networks, scan_time);

  // Scan volumes
  auto volumes = scanner_->list_volumes();
  store_->store_volumes(volumes, scan_time);

  // Discover compose files
  auto compose_files = scanner_->discover_compose_files();
  if (log_) {
    log_->info("agent", "Found " + std::to_string(compose_files.size()) + " compose files");
  }
  store_->store_compose_files(compose_files, scan_time);

  // Record scan
  store_->record_scan(scan_time,
                     containers.size(),
                     images.size(),
                     networks.size(),
                     volumes.size(),
                     compose_files.size());

  if (log_) {
    log_->info("agent", "Docker scan completed");
  }
}

std::string Runtime::build_registration_response() {
  std::ostringstream json;

  json << "{\n";
  json << "  \"server_label\": \"" << opts_.server_label << "\",\n";
  json << "  \"agent_version\": \"0.1.0\",\n";

  // Get system info if scanner is available
  if (scanner_) {
    auto sys_info = scanner_->get_system_info();
    json << "  \"hostname\": \"" << sys_info.hostname << "\",\n";
    json << "  \"system_info\": {\n";
    json << "    \"os\": \"" << sys_info.os << "\",\n";
    json << "    \"arch\": \"" << sys_info.arch << "\",\n";
    json << "    \"docker_version\": \"" << sys_info.docker_version << "\",\n";
    json << "    \"compose_version\": \"" << sys_info.compose_version << "\"\n";
    json << "  },\n";

    // Determine capabilities
    json << "  \"capabilities\": [";
    bool first = true;
    if (scanner_->is_docker_available()) {
      json << "\"docker\"";
      first = false;
    }
    if (!sys_info.compose_version.empty()) {
      if (!first) json << ", ";
      json << "\"compose\"";
      first = false;
    }
    json << "]\n";
  } else {
    json << "  \"hostname\": \"unknown\",\n";
    json << "  \"capabilities\": []\n";
  }

  json << "}\n";

  return json.str();
}

} // namespace nazg::agent

#include "docker_monitor/client.hpp"
#include "agent/protocol.hpp"
#include "blackbox/logger.hpp"
#include "bot/transport.hpp"
#include "bot/types.hpp"
#include "nexus/store.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <netdb.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace nazg::docker_monitor {

Client::Client(::nazg::nexus::Store *store, ::nazg::blackbox::logger *log)
    : store_(store), log_(log) {}

Client::~Client() = default;

bool Client::send_message_to_agent(const ::nazg::bot::HostConfig &host,
                                   uint8_t message_type,
                                   const std::string &payload,
                                   std::string &response) {
  using namespace ::nazg::agent::protocol;

  // Extract host and port
  std::string host_addr = host.address;
  size_t at_pos = host_addr.find('@');
  if (at_pos != std::string::npos) {
    host_addr = host_addr.substr(at_pos + 1);
  }

  std::string port_str = std::to_string(host.agent_port);

  // Resolve address
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo *result = nullptr;
  int rc = ::getaddrinfo(host_addr.c_str(), port_str.c_str(), &hints, &result);
  if (rc != 0 || result == nullptr) {
    if (log_) {
      log_->error("docker_monitor", "Failed to resolve agent address: " +
                  host_addr + ":" + port_str);
    }
    if (result) {
      ::freeaddrinfo(result);
    }
    return false;
  }

  // Connect
  int fd = -1;
  for (addrinfo *ai = result; ai != nullptr; ai = ai->ai_next) {
    fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
      continue;
    }

    timeval timeout{};
    timeout.tv_sec = 5;  // 5 second timeout
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
      break;
    }

    ::close(fd);
    fd = -1;
  }

  ::freeaddrinfo(result);

  if (fd < 0) {
    if (log_) {
      log_->error("docker_monitor", "Failed to connect to agent: " +
                  host_addr + ":" + port_str);
    }
    return false;
  }

  // Send Hello message
  Header hello_header{MessageType::Hello, 0};
  auto hello_packet = encode(hello_header, "nazg-controller");
  if (::send(fd, hello_packet.data(), hello_packet.size(), 0) < 0) {
    ::close(fd);
    return false;
  }

  // Read HelloAck
  Header ack_header{};
  std::string ack_payload;
  {
    std::size_t header_bytes = 0;
    unsigned char *hdr_buf = reinterpret_cast<unsigned char *>(&ack_header);
    while (header_bytes < sizeof(Header)) {
      ssize_t received = ::recv(fd, hdr_buf + header_bytes,
                               sizeof(Header) - header_bytes, 0);
      if (received <= 0) {
        ::close(fd);
        return false;
      }
      header_bytes += static_cast<std::size_t>(received);
    }

    if (ack_header.payload_size > 0) {
      ack_payload.resize(ack_header.payload_size);
      std::size_t offset = 0;
      while (offset < ack_header.payload_size) {
        ssize_t received = ::recv(fd, ack_payload.data() + offset,
                                 ack_header.payload_size - offset, 0);
        if (received <= 0) {
          ::close(fd);
          return false;
        }
        offset += static_cast<std::size_t>(received);
      }
    }
  }

  if (ack_header.type != MessageType::HelloAck) {
    ::close(fd);
    return false;
  }

  // Send actual request message
  Header request_header{static_cast<MessageType>(message_type), 0};
  auto request_packet = encode(request_header, payload);
  if (::send(fd, request_packet.data(), request_packet.size(), 0) < 0) {
    ::close(fd);
    return false;
  }

  // Read response
  Header response_header{};
  {
    std::size_t header_bytes = 0;
    unsigned char *hdr_buf = reinterpret_cast<unsigned char *>(&response_header);
    while (header_bytes < sizeof(Header)) {
      ssize_t received = ::recv(fd, hdr_buf + header_bytes,
                               sizeof(Header) - header_bytes, 0);
      if (received <= 0) {
        ::close(fd);
        return false;
      }
      header_bytes += static_cast<std::size_t>(received);
    }

    if (response_header.payload_size > 0) {
      response.resize(response_header.payload_size);
      std::size_t offset = 0;
      while (offset < response_header.payload_size) {
        ssize_t received = ::recv(fd, response.data() + offset,
                                 response_header.payload_size - offset, 0);
        if (received <= 0) {
          ::close(fd);
          return false;
        }
        offset += static_cast<std::size_t>(received);
      }
    }
  }

  ::close(fd);

  if (response_header.type == MessageType::Error) {
    if (log_) {
      log_->error("docker_monitor", "Agent returned error: " + response);
    }
    return false;
  }

  return true;
}

// Simple JSON parser helpers (very basic, not production-grade)
namespace {

std::string extract_json_string(const std::string &json, const std::string &key) {
  std::string pattern = "\"" + key + "\":";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos) {
    return "";
  }

  pos += pattern.size();
  // Skip whitespace
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
    pos++;
  }

  if (pos >= json.size() || json[pos] != '"') {
    return "";
  }

  pos++; // Skip opening quote
  size_t end = pos;
  while (end < json.size() && json[end] != '"') {
    if (json[end] == '\\') {
      end++; // Skip escaped character
    }
    end++;
  }

  return json.substr(pos, end - pos);
}

int64_t extract_json_int(const std::string &json, const std::string &key) {
  std::string pattern = "\"" + key + "\":";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos) {
    return 0;
  }

  pos += pattern.size();
  // Skip whitespace
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
    pos++;
  }

  size_t end = pos;
  while (end < json.size() && (json[end] >= '0' && json[end] <= '9')) {
    end++;
  }

  if (end == pos) {
    return 0;
  }

  try {
    return std::stoll(json.substr(pos, end - pos));
  } catch (...) {
    return 0;
  }
}

// Extract JSON array (returns the array as a string including brackets)
std::string extract_json_array(const std::string &json, const std::string &key) {
  std::string pattern = "\"" + key + "\":";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos) {
    return "[]";
  }

  pos += pattern.size();
  // Skip whitespace
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) {
    pos++;
  }

  if (pos >= json.size() || json[pos] != '[') {
    return "[]";
  }

  size_t start = pos;
  int bracket_count = 0;
  while (pos < json.size()) {
    if (json[pos] == '[') {
      bracket_count++;
    } else if (json[pos] == ']') {
      bracket_count--;
      if (bracket_count == 0) {
        return json.substr(start, pos - start + 1);
      }
    }
    pos++;
  }

  return "[]";
}

} // anonymous namespace

bool Client::parse_and_store_scan(int64_t server_id,
                                  const std::string &server_label,
                                  const std::string &json_payload,
                                  ScanResult &result) {
  if (!store_) {
    return false;
  }

  auto now = std::chrono::system_clock::now();
  auto scan_time = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  if (log_) {
    log_->info("docker_monitor", "Parsing Docker scan from " + server_label +
               " (" + std::to_string(json_payload.size()) + " bytes)");
  }

  // Extract containers array
  std::string containers_json = extract_json_array(json_payload, "containers");

  // Parse each container (very simplified - in production use a real JSON library)
  size_t pos = 1; // Skip opening bracket
  while (pos < containers_json.size()) {
    // Find next container object
    size_t obj_start = containers_json.find('{', pos);
    if (obj_start == std::string::npos) break;

    size_t obj_end = obj_start + 1;
    int brace_count = 1;
    while (obj_end < containers_json.size() && brace_count > 0) {
      if (containers_json[obj_end] == '{') brace_count++;
      else if (containers_json[obj_end] == '}') brace_count--;
      obj_end++;
    }

    std::string container_obj = containers_json.substr(obj_start, obj_end - obj_start);

    // Extract container fields
    std::string id = extract_json_string(container_obj, "id");
    std::string name = extract_json_string(container_obj, "name");
    std::string image = extract_json_string(container_obj, "image");
    std::string state = extract_json_string(container_obj, "state");
    std::string status = extract_json_string(container_obj, "status");
    int64_t created = extract_json_int(container_obj, "created");
    std::string service_name = extract_json_string(container_obj, "service_name");
    std::string health_status = extract_json_string(container_obj, "health_status");
    std::string restart_policy = extract_json_string(container_obj, "restart_policy");

    if (!id.empty()) {
      store_->upsert_container(server_id, id, name, image, state, status,
                               created, "[]", "[]", "[]", service_name, "[]",
                               "{}", health_status, restart_policy);
      result.containers_count++;
    }

    pos = obj_end;
  }

  // Extract and store images
  std::string images_json = extract_json_array(json_payload, "images");
  pos = 1;
  while (pos < images_json.size()) {
    size_t obj_start = images_json.find('{', pos);
    if (obj_start == std::string::npos) break;

    size_t obj_end = obj_start + 1;
    int brace_count = 1;
    while (obj_end < images_json.size() && brace_count > 0) {
      if (images_json[obj_end] == '{') brace_count++;
      else if (images_json[obj_end] == '}') brace_count--;
      obj_end++;
    }

    std::string image_obj = images_json.substr(obj_start, obj_end - obj_start);

    std::string id = extract_json_string(image_obj, "id");
    std::string repository = extract_json_string(image_obj, "repository");
    std::string tag = extract_json_string(image_obj, "tag");
    int64_t size_bytes = extract_json_int(image_obj, "size_bytes");
    int64_t created = extract_json_int(image_obj, "created");

    if (!id.empty()) {
      store_->upsert_docker_image(server_id, id, repository, tag, size_bytes, created);
      result.images_count++;
    }

    pos = obj_end;
  }

  // Extract and store compose files
  std::string compose_json = extract_json_array(json_payload, "compose_files");
  pos = 1;
  while (pos < compose_json.size()) {
    size_t obj_start = compose_json.find('{', pos);
    if (obj_start == std::string::npos) break;

    size_t obj_end = obj_start + 1;
    int brace_count = 1;
    while (obj_end < compose_json.size() && brace_count > 0) {
      if (compose_json[obj_end] == '{') brace_count++;
      else if (compose_json[obj_end] == '}') brace_count--;
      obj_end++;
    }

    std::string compose_obj = compose_json.substr(obj_start, obj_end - obj_start);

    std::string path = extract_json_string(compose_obj, "path");
    std::string project_name = extract_json_string(compose_obj, "project_name");
    std::string services = extract_json_array(compose_obj, "services");
    std::string hash = extract_json_string(compose_obj, "hash");

    if (!path.empty()) {
      store_->upsert_compose_file(server_id, path, project_name, services,
                                   "[]", "[]", hash);
      result.compose_files_count++;
    }

    pos = obj_end;
  }

  // Extract and store networks
  std::string networks_json = extract_json_array(json_payload, "networks");
  pos = 1;
  while (pos < networks_json.size()) {
    size_t obj_start = networks_json.find('{', pos);
    if (obj_start == std::string::npos) break;

    size_t obj_end = obj_start + 1;
    int brace_count = 1;
    while (obj_end < networks_json.size() && brace_count > 0) {
      if (networks_json[obj_end] == '{') brace_count++;
      else if (networks_json[obj_end] == '}') brace_count--;
      obj_end++;
    }

    std::string network_obj = networks_json.substr(obj_start, obj_end - obj_start);

    std::string id = extract_json_string(network_obj, "id");
    std::string name = extract_json_string(network_obj, "name");
    std::string driver = extract_json_string(network_obj, "driver");
    std::string scope = extract_json_string(network_obj, "scope");

    if (!id.empty()) {
      store_->upsert_docker_network(server_id, id, name, driver, scope, "{}");
      result.networks_count++;
    }

    pos = obj_end;
  }

  // Extract and store volumes
  std::string volumes_json = extract_json_array(json_payload, "volumes");
  pos = 1;
  while (pos < volumes_json.size()) {
    size_t obj_start = volumes_json.find('{', pos);
    if (obj_start == std::string::npos) break;

    size_t obj_end = obj_start + 1;
    int brace_count = 1;
    while (obj_end < volumes_json.size() && brace_count > 0) {
      if (volumes_json[obj_end] == '{') brace_count++;
      else if (volumes_json[obj_end] == '}') brace_count--;
      obj_end++;
    }

    std::string volume_obj = volumes_json.substr(obj_start, obj_end - obj_start);

    std::string name = extract_json_string(volume_obj, "name");
    std::string driver = extract_json_string(volume_obj, "driver");
    std::string mountpoint = extract_json_string(volume_obj, "mountpoint");

    if (!name.empty()) {
      store_->upsert_docker_volume(server_id, name, driver, mountpoint, "{}");
      result.volumes_count++;
    }

    pos = obj_end;
  }

  if (log_) {
    log_->info("docker_monitor",
               "Stored scan results: " + std::to_string(result.containers_count) +
               " containers, " + std::to_string(result.images_count) + " images, " +
               std::to_string(result.compose_files_count) + " compose files");
  }

  result.success = true;
  return true;
}

bool Client::parse_and_store_registration(int64_t server_id,
                                           const std::string &json_payload) {
  if (!store_) {
    return false;
  }

  std::string agent_version = extract_json_string(json_payload, "agent_version");
  std::string capabilities = extract_json_array(json_payload, "capabilities");

  return store_->update_server_heartbeat(server_id, agent_version, "online",
                                          capabilities);
}

ScanResult Client::scan_server(const std::string &server_label) {
  ScanResult result;

  if (!store_) {
    result.error_message = "No store available";
    return result;
  }

  // Get server info from database
  auto server_id_opt = store_->get_server_id(server_label);
  if (!server_id_opt.has_value()) {
    result.error_message = "Server not found: " + server_label;
    return result;
  }

  int64_t server_id = server_id_opt.value();
  auto server_info = store_->get_server_by_id(server_id);
  if (!server_info.has_value()) {
    result.error_message = "Failed to retrieve server info";
    return result;
  }

  // Build HostConfig
  ::nazg::bot::HostConfig host;
  host.label = server_label;
  host.address = server_info.value()["host"];
  host.agent_port = 7070; // Default agent port
  // Parse SSH config if needed

  if (log_) {
    log_->info("docker_monitor", "Scanning server: " + server_label);
  }

  // Send DockerFullScan request
  std::string response;
  bool success = send_message_to_agent(
      host,
      static_cast<uint8_t>(::nazg::agent::protocol::MessageType::DockerFullScan),
      "",
      response);

  if (!success) {
    result.error_message = "Failed to connect to agent or receive response";
    store_->update_server_status(server_id, "offline");
    return result;
  }

  // Parse and store the response
  if (!parse_and_store_scan(server_id, server_label, response, result)) {
    result.error_message = "Failed to parse scan results";
    return result;
  }

  // Update server heartbeat
  store_->update_server_status(server_id, "online");

  return result;
}

bool Client::register_agent(const std::string &server_label) {
  if (!store_) {
    return false;
  }

  // Get server info
  auto server_id_opt = store_->get_server_id(server_label);
  if (!server_id_opt.has_value()) {
    return false;
  }

  int64_t server_id = server_id_opt.value();
  auto server_info = store_->get_server_by_id(server_id);
  if (!server_info.has_value()) {
    return false;
  }

  // Build HostConfig
  ::nazg::bot::HostConfig host;
  host.label = server_label;
  host.address = server_info.value()["host"];
  host.agent_port = 7070;

  // Send Register request
  std::string response;
  bool success = send_message_to_agent(
      host,
      static_cast<uint8_t>(::nazg::agent::protocol::MessageType::Register),
      "",
      response);

  if (!success) {
    store_->update_server_status(server_id, "offline");
    return false;
  }

  // Parse and store registration
  if (!parse_and_store_registration(server_id, response)) {
    return false;
  }

  if (log_) {
    log_->info("docker_monitor", "Registered agent: " + server_label);
  }

  return true;
}

std::optional<std::string> Client::get_scan_stats(const std::string &server_label) {
  if (!store_) {
    return std::nullopt;
  }

  auto server_id_opt = store_->get_server_id(server_label);
  if (!server_id_opt.has_value()) {
    return std::nullopt;
  }

  int64_t server_id = server_id_opt.value();

  auto containers = store_->list_containers(server_id);
  auto images = store_->list_docker_images(server_id);
  auto networks = store_->list_docker_networks(server_id);
  auto volumes = store_->list_docker_volumes(server_id);
  auto compose_files = store_->list_compose_files(server_id);

  std::ostringstream stats;
  stats << "Server: " << server_label << "\n";
  stats << "  Containers: " << containers.size() << "\n";
  stats << "  Images: " << images.size() << "\n";
  stats << "  Networks: " << networks.size() << "\n";
  stats << "  Volumes: " << volumes.size() << "\n";
  stats << "  Compose files: " << compose_files.size() << "\n";

  return stats.str();
}

} // namespace nazg::docker_monitor

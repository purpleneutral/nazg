#include "agent/docker_scanner.hpp"
#include "blackbox/logger.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <openssl/sha.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace nazg::agent {

DockerScanner::DockerScanner(::nazg::blackbox::logger *log) : log_(log) {}

std::string DockerScanner::exec_command(const std::string &cmd) {
  std::array<char, 256> buffer;
  std::string result;

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    if (log_) {
      log_->error("docker_scanner", "Failed to execute command: " + cmd);
    }
    return "";
  }

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }

  int status = pclose(pipe);
  if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
    if (log_) {
      log_->warn("docker_scanner", "Command exited with code " +
                 std::to_string(WEXITSTATUS(status)) + ": " + cmd);
    }
  }

  return result;
}

bool DockerScanner::is_docker_available() {
  std::string result = exec_command("docker --version 2>/dev/null");
  return !result.empty();
}

SystemInfo DockerScanner::get_system_info() {
  SystemInfo info;

  // Hostname
  char hostname_buf[256];
  if (gethostname(hostname_buf, sizeof(hostname_buf)) == 0) {
    info.hostname = hostname_buf;
  }

  // OS and architecture
  info.os = exec_command("uname -s 2>/dev/null");
  info.arch = exec_command("uname -m 2>/dev/null");

  // Trim newlines
  auto trim = [](std::string &s) {
    s.erase(s.find_last_not_of(" \n\r\t") + 1);
  };
  trim(info.os);
  trim(info.arch);

  // Docker version
  std::string docker_ver = exec_command("docker --version 2>/dev/null");
  if (!docker_ver.empty()) {
    // Extract version from "Docker version 24.0.5, build ..."
    size_t pos = docker_ver.find("version ");
    if (pos != std::string::npos) {
      pos += 8;
      size_t end = docker_ver.find(",", pos);
      if (end == std::string::npos) end = docker_ver.find("\n", pos);
      info.docker_version = docker_ver.substr(pos, end - pos);
    }
  }

  // Compose version
  std::string compose_ver = exec_command("docker compose version 2>/dev/null");
  if (!compose_ver.empty()) {
    // Extract version from "Docker Compose version v2.20.0"
    size_t pos = compose_ver.find("version ");
    if (pos != std::string::npos) {
      pos += 8;
      size_t end = compose_ver.find("\n", pos);
      info.compose_version = compose_ver.substr(pos, end - pos);
    }
  }

  return info;
}

std::string DockerScanner::escape_json(const std::string &str) {
  std::string result;
  result.reserve(str.size());

  for (char c : str) {
    switch (c) {
      case '"':  result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 32) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          result += buf;
        } else {
          result += c;
        }
    }
  }

  return result;
}

std::vector<ContainerInfo> DockerScanner::list_containers() {
  std::vector<ContainerInfo> containers;

  // Use docker ps -a with JSON format for complete information
  std::string output = exec_command(
    "docker ps -a --no-trunc --format "
    "'{{json .}}' 2>/dev/null"
  );

  if (output.empty()) {
    return containers;
  }

  std::istringstream stream(output);
  std::string line;

  while (std::getline(stream, line)) {
    if (line.empty() || line[0] != '{') continue;

    ContainerInfo info;

    // Parse JSON-like output from docker ps
    // This is a simple parser - for production, use a JSON library
    auto extract = [&](const std::string &key) -> std::string {
      std::string pattern = "\"" + key + "\":\"";
      size_t pos = line.find(pattern);
      if (pos == std::string::npos) return "";
      pos += pattern.size();
      size_t end = line.find("\"", pos);
      if (end == std::string::npos) return "";
      return line.substr(pos, end - pos);
    };

    info.id = extract("ID");
    if (info.id.length() > 12) info.id = info.id.substr(0, 12);
    info.name = extract("Names");
    info.image = extract("Image");
    info.state = extract("State");
    info.status = extract("Status");

    // Get detailed inspect data
    std::string inspect_cmd = "docker inspect " + info.id + " 2>/dev/null";
    std::string inspect_json = exec_command(inspect_cmd);

    if (!inspect_json.empty()) {
      // Extract created timestamp
      std::string created_str = exec_command(
        "docker inspect -f '{{.Created}}' " + info.id + " 2>/dev/null"
      );
      // Parse ISO8601 timestamp - simplified version
      // For production, use proper datetime parsing
      info.created = std::time(nullptr); // Placeholder

      // Get ports, volumes, networks from inspect
      info.ports_json = "[]"; // Simplified - would need proper JSON parsing
      info.volumes_json = "[]";
      info.networks_json = "[]";

      // Extract labels for compose detection
      std::string labels = exec_command(
        "docker inspect -f '{{json .Config.Labels}}' " + info.id + " 2>/dev/null"
      );
      info.labels_json = labels.empty() ? "{}" : labels;

      // Check for compose labels
      if (labels.find("com.docker.compose.project") != std::string::npos) {
        info.service_name = exec_command(
          "docker inspect -f '{{index .Config.Labels \"com.docker.compose.service\"}}' " +
          info.id + " 2>/dev/null"
        );
        // Trim newline
        if (!info.service_name.empty() && info.service_name.back() == '\n') {
          info.service_name.pop_back();
        }
      }

      // Health status
      info.health_status = exec_command(
        "docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' " +
        info.id + " 2>/dev/null"
      );
      if (!info.health_status.empty() && info.health_status.back() == '\n') {
        info.health_status.pop_back();
      }

      // Restart policy
      info.restart_policy = exec_command(
        "docker inspect -f '{{.HostConfig.RestartPolicy.Name}}' " +
        info.id + " 2>/dev/null"
      );
      if (!info.restart_policy.empty() && info.restart_policy.back() == '\n') {
        info.restart_policy.pop_back();
      }
    }

    containers.push_back(info);
  }

  return containers;
}

ContainerInfo DockerScanner::inspect_container(const std::string &name_or_id) {
  ContainerInfo info;

  std::string cmd = "docker inspect " + name_or_id + " 2>/dev/null";
  std::string output = exec_command(cmd);

  if (output.empty()) {
    return info;
  }

  // Extract key fields - simplified version
  info.id = name_or_id;
  info.name = exec_command(
    "docker inspect -f '{{.Name}}' " + name_or_id + " 2>/dev/null"
  );

  return info;
}

std::vector<DockerImageInfo> DockerScanner::list_images() {
  std::vector<DockerImageInfo> images;

  std::string output = exec_command(
    "docker images --no-trunc --format "
    "'{{.ID}}|{{.Repository}}|{{.Tag}}|{{.Size}}|{{.CreatedAt}}' 2>/dev/null"
  );

  if (output.empty()) {
    return images;
  }

  std::istringstream stream(output);
  std::string line;

  while (std::getline(stream, line)) {
    if (line.empty()) continue;

    std::istringstream line_stream(line);
    std::string field;
    DockerImageInfo info;
    int field_idx = 0;

    while (std::getline(line_stream, field, '|')) {
      switch (field_idx) {
        case 0:
          info.id = field.substr(0, 12);
          break;
        case 1:
          info.repository = field;
          break;
        case 2:
          info.tag = field;
          break;
        case 3:
          // Parse size (e.g., "1.2GB" -> bytes)
          // Simplified - would need proper parsing
          info.size_bytes = 0;
          break;
        case 4:
          // Parse created timestamp - simplified
          info.created = std::time(nullptr);
          break;
      }
      field_idx++;
    }

    images.push_back(info);
  }

  return images;
}

std::vector<DockerNetworkInfo> DockerScanner::list_networks() {
  std::vector<DockerNetworkInfo> networks;

  std::string output = exec_command(
    "docker network ls --no-trunc --format "
    "'{{.ID}}|{{.Name}}|{{.Driver}}|{{.Scope}}' 2>/dev/null"
  );

  if (output.empty()) {
    return networks;
  }

  std::istringstream stream(output);
  std::string line;

  while (std::getline(stream, line)) {
    if (line.empty()) continue;

    std::istringstream line_stream(line);
    std::string field;
    DockerNetworkInfo info;
    int field_idx = 0;

    while (std::getline(line_stream, field, '|')) {
      switch (field_idx) {
        case 0: info.id = field; break;
        case 1: info.name = field; break;
        case 2: info.driver = field; break;
        case 3: info.scope = field; break;
      }
      field_idx++;
    }

    networks.push_back(info);
  }

  return networks;
}

std::vector<DockerVolumeInfo> DockerScanner::list_volumes() {
  std::vector<DockerVolumeInfo> volumes;

  std::string output = exec_command(
    "docker volume ls --format "
    "'{{.Name}}|{{.Driver}}|{{.Mountpoint}}' 2>/dev/null"
  );

  if (output.empty()) {
    return volumes;
  }

  std::istringstream stream(output);
  std::string line;

  while (std::getline(stream, line)) {
    if (line.empty()) continue;

    std::istringstream line_stream(line);
    std::string field;
    DockerVolumeInfo info;
    int field_idx = 0;

    while (std::getline(line_stream, field, '|')) {
      switch (field_idx) {
        case 0: info.name = field; break;
        case 1: info.driver = field; break;
        case 2: info.mountpoint = field; break;
      }
      field_idx++;
    }

    volumes.push_back(info);
  }

  return volumes;
}

std::string DockerScanner::calculate_file_hash(const std::string &file_path) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    return "";
  }

  SHA256_CTX ctx;
  SHA256_Init(&ctx);

  char buffer[8192];
  while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
    SHA256_Update(&ctx, buffer, file.gcount());
  }

  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256_Final(hash, &ctx);

  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    oss << std::setw(2) << static_cast<int>(hash[i]);
  }

  return oss.str();
}

std::vector<ComposeFileInfo> DockerScanner::discover_compose_files(
    const std::vector<std::string> &search_paths) {

  std::vector<ComposeFileInfo> compose_files;

  for (const auto &base_path : search_paths) {
    try {
      if (!fs::exists(base_path) || !fs::is_directory(base_path)) {
        continue;
      }

      // Search recursively for docker-compose files
      for (const auto &entry : fs::recursive_directory_iterator(
            base_path,
            fs::directory_options::skip_permission_denied)) {

        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();

        // Match common compose file names
        if (filename == "docker-compose.yml" ||
            filename == "docker-compose.yaml" ||
            filename == "compose.yml" ||
            filename == "compose.yaml") {

          ComposeFileInfo info;
          info.path = entry.path().string();

          // Try to extract project name from parent directory or file
          info.project_name = entry.path().parent_path().filename().string();

          // Calculate file hash
          info.file_hash = calculate_file_hash(info.path);

          // Parse services from compose file (simplified)
          std::ifstream compose_file(info.path);
          std::string line;
          std::vector<std::string> services;
          bool in_services = false;

          while (std::getline(compose_file, line)) {
            if (line.find("services:") != std::string::npos) {
              in_services = true;
              continue;
            }

            if (in_services && !line.empty() && line[0] != ' ' && line[0] != '\t') {
              // Reached next top-level section
              break;
            }

            if (in_services && line.find("  ") == 0) {
              // This is a service definition
              size_t colon_pos = line.find(':');
              if (colon_pos != std::string::npos) {
                std::string service = line.substr(2, colon_pos - 2);
                // Trim whitespace
                service.erase(0, service.find_first_not_of(" \t"));
                service.erase(service.find_last_not_of(" \t") + 1);
                if (!service.empty()) {
                  services.push_back(service);
                }
              }
            }
          }

          // Convert services to JSON array
          std::ostringstream services_json;
          services_json << "[";
          for (size_t i = 0; i < services.size(); i++) {
            if (i > 0) services_json << ",";
            services_json << "\"" << escape_json(services[i]) << "\"";
          }
          services_json << "]";
          info.services_json = services_json.str();

          compose_files.push_back(info);
        }
      }
    } catch (const fs::filesystem_error &e) {
      if (log_) {
        log_->warn("docker_scanner",
                   "Failed to scan directory " + base_path + ": " + e.what());
      }
    }
  }

  return compose_files;
}

std::string DockerScanner::generate_full_scan_json(const std::string &server_label) {
  std::ostringstream json;
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  json << "{\n";
  json << "  \"server_label\": \"" << escape_json(server_label) << "\",\n";
  json << "  \"timestamp\": " << timestamp << ",\n";

  // Containers
  json << "  \"containers\": [\n";
  auto containers = list_containers();
  for (size_t i = 0; i < containers.size(); i++) {
    const auto &c = containers[i];
    if (i > 0) json << ",\n";
    json << "    {\n";
    json << "      \"id\": \"" << escape_json(c.id) << "\",\n";
    json << "      \"name\": \"" << escape_json(c.name) << "\",\n";
    json << "      \"image\": \"" << escape_json(c.image) << "\",\n";
    json << "      \"state\": \"" << escape_json(c.state) << "\",\n";
    json << "      \"status\": \"" << escape_json(c.status) << "\",\n";
    json << "      \"created\": " << c.created << ",\n";
    json << "      \"service_name\": \"" << escape_json(c.service_name) << "\",\n";
    json << "      \"health_status\": \"" << escape_json(c.health_status) << "\",\n";
    json << "      \"restart_policy\": \"" << escape_json(c.restart_policy) << "\"\n";
    json << "    }";
  }
  json << "\n  ],\n";

  // Compose files
  json << "  \"compose_files\": [\n";
  auto compose_files = discover_compose_files();
  for (size_t i = 0; i < compose_files.size(); i++) {
    const auto &cf = compose_files[i];
    if (i > 0) json << ",\n";
    json << "    {\n";
    json << "      \"path\": \"" << escape_json(cf.path) << "\",\n";
    json << "      \"project_name\": \"" << escape_json(cf.project_name) << "\",\n";
    json << "      \"services\": " << cf.services_json << ",\n";
    json << "      \"hash\": \"" << escape_json(cf.file_hash) << "\"\n";
    json << "    }";
  }
  json << "\n  ],\n";

  // Images
  json << "  \"images\": [\n";
  auto images = list_images();
  for (size_t i = 0; i < images.size(); i++) {
    const auto &img = images[i];
    if (i > 0) json << ",\n";
    json << "    {\n";
    json << "      \"id\": \"" << escape_json(img.id) << "\",\n";
    json << "      \"repository\": \"" << escape_json(img.repository) << "\",\n";
    json << "      \"tag\": \"" << escape_json(img.tag) << "\",\n";
    json << "      \"size_bytes\": " << img.size_bytes << ",\n";
    json << "      \"created\": " << img.created << "\n";
    json << "    }";
  }
  json << "\n  ],\n";

  // Networks
  json << "  \"networks\": [\n";
  auto networks = list_networks();
  for (size_t i = 0; i < networks.size(); i++) {
    const auto &net = networks[i];
    if (i > 0) json << ",\n";
    json << "    {\n";
    json << "      \"id\": \"" << escape_json(net.id) << "\",\n";
    json << "      \"name\": \"" << escape_json(net.name) << "\",\n";
    json << "      \"driver\": \"" << escape_json(net.driver) << "\",\n";
    json << "      \"scope\": \"" << escape_json(net.scope) << "\"\n";
    json << "    }";
  }
  json << "\n  ],\n";

  // Volumes
  json << "  \"volumes\": [\n";
  auto volumes = list_volumes();
  for (size_t i = 0; i < volumes.size(); i++) {
    const auto &vol = volumes[i];
    if (i > 0) json << ",\n";
    json << "    {\n";
    json << "      \"name\": \"" << escape_json(vol.name) << "\",\n";
    json << "      \"driver\": \"" << escape_json(vol.driver) << "\",\n";
    json << "      \"mountpoint\": \"" << escape_json(vol.mountpoint) << "\"\n";
    json << "    }";
  }
  json << "\n  ]\n";

  json << "}\n";

  return json.str();
}

} // namespace nazg::agent

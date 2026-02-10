#include "docker_monitor/compose_parser.hpp"
#include "blackbox/logger.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace nazg::docker_monitor {

ComposeParser::ComposeParser(::nazg::blackbox::logger *log) : log_(log) {}

std::string ComposeParser::trim(const std::string &str) {
  size_t start = 0;
  while (start < str.size() && (str[start] == ' ' || str[start] == '\t')) {
    start++;
  }

  size_t end = str.size();
  while (end > start && (str[end - 1] == ' ' || str[end - 1] == '\t' ||
                        str[end - 1] == '\r' || str[end - 1] == '\n')) {
    end--;
  }

  return str.substr(start, end - start);
}

int ComposeParser::get_indent_level(const std::string &line) {
  int indent = 0;
  for (char c : line) {
    if (c == ' ') indent++;
    else if (c == '\t') indent += 2;
    else break;
  }
  return indent;
}

std::string ComposeParser::extract_key(const std::string &line) {
  std::string trimmed = trim(line);
  size_t colon = trimmed.find(':');
  if (colon == std::string::npos) {
    return trimmed;
  }
  return trim(trimmed.substr(0, colon));
}

std::string ComposeParser::extract_value(const std::string &line) {
  std::string trimmed = trim(line);
  size_t colon = trimmed.find(':');
  if (colon == std::string::npos) {
    return "";
  }
  std::string value = trim(trimmed.substr(colon + 1));

  // Remove quotes if present
  if (value.size() >= 2) {
    if ((value.front() == '"' && value.back() == '"') ||
        (value.front() == '\'' && value.back() == '\'')) {
      return value.substr(1, value.size() - 2);
    }
  }

  return value;
}

bool ComposeParser::is_list_item(const std::string &line) {
  std::string trimmed = trim(line);
  return !trimmed.empty() && trimmed[0] == '-';
}

std::string ComposeParser::extract_list_item(const std::string &line) {
  std::string trimmed = trim(line);
  if (trimmed.empty() || trimmed[0] != '-') {
    return "";
  }
  return trim(trimmed.substr(1));
}

void ComposeParser::parse_service_networks(const std::vector<std::string> &lines,
                                           size_t &pos,
                                           ComposeService &service) {
  int networks_indent = get_indent_level(lines[pos]);
  pos++;

  while (pos < lines.size()) {
    const std::string &line = lines[pos];
    if (trim(line).empty() || line[0] == '#') {
      pos++;
      continue;
    }

    int indent = get_indent_level(line);
    if (indent <= networks_indent) {
      break;
    }

    // Check if it's a list item (array of network names)
    if (is_list_item(line)) {
      std::string network = extract_list_item(line);
      service.networks.push_back(network);
      pos++;
    }
    // Or an object (network with configuration)
    else {
      std::string network_name = extract_key(line);
      service.networks.push_back(network_name);
      pos++;

      // Look for ipv4_address under this network
      while (pos < lines.size()) {
        const std::string &config_line = lines[pos];
        if (trim(config_line).empty() || config_line[0] == '#') {
          pos++;
          continue;
        }

        int config_indent = get_indent_level(config_line);
        if (config_indent <= indent) {
          break;
        }

        std::string key = extract_key(config_line);
        if (key == "ipv4_address" || key == "ipv6_address") {
          std::string ip = extract_value(config_line);
          service.network_static_ips[network_name] = ip;
        }
        pos++;
      }
    }
  }
}

void ComposeParser::parse_service(const std::vector<std::string> &lines,
                                  size_t &pos,
                                  const std::string &service_name,
                                  ComposeService &service) {
  service.name = service_name;
  int service_indent = get_indent_level(lines[pos]);
  pos++;

  while (pos < lines.size()) {
    const std::string &line = lines[pos];

    // Skip empty lines and comments
    if (trim(line).empty() || trim(line)[0] == '#') {
      pos++;
      continue;
    }

    int indent = get_indent_level(line);

    // End of this service
    if (indent <= service_indent) {
      break;
    }

    std::string key = extract_key(line);
    std::string value = extract_value(line);

    if (key == "image") {
      service.image = value;
      pos++;
    }
    else if (key == "container_name") {
      service.container_name = value;
      pos++;
    }
    else if (key == "network_mode") {
      service.network_mode = value;
      pos++;
    }
    else if (key == "restart") {
      service.restart_policy = value;
      pos++;
    }
    else if (key == "privileged") {
      service.privileged = (value == "true");
      pos++;
    }
    else if (key == "depends_on") {
      pos++;
      // Parse list of dependencies
      while (pos < lines.size()) {
        const std::string &dep_line = lines[pos];
        if (trim(dep_line).empty() || dep_line[0] == '#') {
          pos++;
          continue;
        }

        int dep_indent = get_indent_level(dep_line);
        if (dep_indent <= indent) {
          break;
        }

        if (is_list_item(dep_line)) {
          std::string dep = extract_list_item(dep_line);
          if (!dep.empty()) {
            service.depends_on.push_back(dep);
          }
        }
        pos++;
      }
    }
    else if (key == "networks") {
      parse_service_networks(lines, pos, service);
    }
    else if (key == "volumes") {
      pos++;
      // Parse volume list
      while (pos < lines.size()) {
        const std::string &vol_line = lines[pos];
        if (trim(vol_line).empty() || vol_line[0] == '#') {
          pos++;
          continue;
        }

        int vol_indent = get_indent_level(vol_line);
        if (vol_indent <= indent) {
          break;
        }

        if (is_list_item(vol_line)) {
          std::string volume = extract_list_item(vol_line);
          if (!volume.empty()) {
            service.volumes.push_back(volume);
          }
        }
        pos++;
      }
    }
    else if (key == "environment") {
      pos++;
      // Parse environment variables
      while (pos < lines.size()) {
        const std::string &env_line = lines[pos];
        if (trim(env_line).empty() || env_line[0] == '#') {
          pos++;
          continue;
        }

        int env_indent = get_indent_level(env_line);
        if (env_indent <= indent) {
          break;
        }

        if (is_list_item(env_line)) {
          std::string env = extract_list_item(env_line);
          size_t eq = env.find('=');
          if (eq != std::string::npos) {
            std::string env_key = env.substr(0, eq);
            std::string env_val = env.substr(eq + 1);
            service.environment[env_key] = env_val;
          }
        } else {
          std::string env_key = extract_key(env_line);
          std::string env_val = extract_value(env_line);
          service.environment[env_key] = env_val;
        }
        pos++;
      }
    }
    else {
      // Unknown key, skip section
      pos++;
      int skip_indent = indent;
      while (pos < lines.size()) {
        const std::string &skip_line = lines[pos];
        if (trim(skip_line).empty() || skip_line[0] == '#') {
          pos++;
          continue;
        }
        int next_indent = get_indent_level(skip_line);
        if (next_indent <= skip_indent) {
          break;
        }
        pos++;
      }
    }
  }
}

void ComposeParser::parse_networks_section(const std::vector<std::string> &lines,
                                           size_t &pos,
                                           std::map<std::string, ComposeNetwork> &networks) {
  int networks_indent = get_indent_level(lines[pos]);
  pos++;

  while (pos < lines.size()) {
    const std::string &line = lines[pos];

    if (trim(line).empty() || trim(line)[0] == '#') {
      pos++;
      continue;
    }

    int indent = get_indent_level(line);
    if (indent <= networks_indent) {
      break;
    }

    std::string network_name = extract_key(line);
    std::string value = extract_value(line);

    ComposeNetwork network;
    network.name = network_name;

    // If there's a value, it might be "external: true"
    if (value == "external") {
      network.external = true;
      pos++;
    } else {
      pos++;

      // Parse network configuration
      while (pos < lines.size()) {
        const std::string &net_line = lines[pos];
        if (trim(net_line).empty() || net_line[0] == '#') {
          pos++;
          continue;
        }

        int net_indent = get_indent_level(net_line);
        if (net_indent <= indent) {
          break;
        }

        std::string key = extract_key(net_line);
        std::string val = extract_value(net_line);

        if (key == "driver") {
          network.driver = val;
        } else if (key == "external") {
          network.external = (val == "true");
        }

        pos++;
      }
    }

    networks[network_name] = network;
  }
}

std::optional<ComposeFile> ComposeParser::parse(const std::string &yaml_content) {
  last_error_.clear();

  ComposeFile compose;

  // Split into lines
  std::vector<std::string> lines;
  std::istringstream stream(yaml_content);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }

  size_t pos = 0;
  while (pos < lines.size()) {
    line = lines[pos];

    // Skip empty lines and comments
    if (trim(line).empty() || trim(line)[0] == '#') {
      pos++;
      continue;
    }

    std::string key = extract_key(line);

    if (key == "version") {
      compose.version = extract_value(line);
      pos++;
    }
    else if (key == "services") {
      int services_indent = get_indent_level(line);
      pos++;

      // Parse each service
      while (pos < lines.size()) {
        const std::string &service_line = lines[pos];

        if (trim(service_line).empty() || service_line[0] == '#') {
          pos++;
          continue;
        }

        int indent = get_indent_level(service_line);
        if (indent <= services_indent) {
          break;
        }

        std::string service_name = extract_key(service_line);
        ComposeService service;
        parse_service(lines, pos, service_name, service);
        compose.services[service_name] = service;
      }
    }
    else if (key == "networks") {
      parse_networks_section(lines, pos, compose.networks);
    }
    else if (key == "volumes") {
      int volumes_indent = get_indent_level(line);
      pos++;

      // Parse volumes
      while (pos < lines.size()) {
        const std::string &vol_line = lines[pos];

        if (trim(vol_line).empty() || vol_line[0] == '#') {
          pos++;
          continue;
        }

        int indent = get_indent_level(vol_line);
        if (indent <= volumes_indent) {
          break;
        }

        std::string volume_name = extract_key(vol_line);
        compose.volumes[volume_name] = "";  // Simplified
        pos++;
      }
    }
    else {
      // Unknown top-level key, skip
      pos++;
    }
  }

  if (log_) {
    log_->info("compose_parser",
               "Parsed compose file: " + std::to_string(compose.services.size()) +
               " services, " + std::to_string(compose.networks.size()) + " networks");
  }

  return compose;
}

std::optional<ComposeFile> ComposeParser::parse_file(const std::string &file_path) {
  std::ifstream file(file_path);
  if (!file) {
    last_error_ = "Failed to open file: " + file_path;
    if (log_) {
      log_->error("compose_parser", last_error_);
    }
    return std::nullopt;
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  return parse(content);
}

std::vector<DetectedDependency> ComposeParser::extract_dependencies(const ComposeFile &compose) {
  std::vector<DetectedDependency> dependencies;

  for (const auto &[service_name, service] : compose.services) {
    // 1. Direct depends_on dependencies
    for (const auto &dep : service.depends_on) {
      DetectedDependency d;
      d.service = service_name;
      d.depends_on = dep;
      d.type = "depends_on";
      d.details = "Explicit dependency in compose file";
      dependencies.push_back(d);
    }

    // 2. Network mode dependencies (e.g., network_mode: "service:gluetun")
    if (!service.network_mode.empty()) {
      if (service.network_mode.substr(0, 8) == "service:") {
        std::string dep_service = service.network_mode.substr(8);
        DetectedDependency d;
        d.service = service_name;
        d.depends_on = dep_service;
        d.type = "network_mode";
        d.details = "Uses network stack of " + dep_service;
        dependencies.push_back(d);
      }
    }

    // 3. Shared network dependencies with static IPs
    //    Services on the same network are loosely coupled
    for (const auto &network : service.networks) {
      // Find other services on the same network
      for (const auto &[other_service_name, other_service] : compose.services) {
        if (other_service_name == service_name) continue;

        auto it = std::find(other_service.networks.begin(),
                           other_service.networks.end(),
                           network);
        if (it != other_service.networks.end()) {
          // Both services are on the same network
          DetectedDependency d;
          d.service = service_name;
          d.depends_on = other_service_name;
          d.type = "shared_network";
          d.details = "Both on network: " + network;

          // Add static IP info if present
          if (service.network_static_ips.count(network)) {
            d.details += " (static IP: " + service.network_static_ips.at(network) + ")";
          }

          dependencies.push_back(d);
        }
      }
    }

    // 4. Volume dependencies (services sharing volumes)
    for (const auto &volume : service.volumes) {
      // Extract volume name (before ':')
      std::string volume_name = volume;
      size_t colon = volume.find(':');
      if (colon != std::string::npos) {
        volume_name = volume.substr(0, colon);
      }

      // Find other services using the same volume
      for (const auto &[other_service_name, other_service] : compose.services) {
        if (other_service_name == service_name) continue;

        for (const auto &other_volume : other_service.volumes) {
          std::string other_volume_name = other_volume;
          size_t other_colon = other_volume.find(':');
          if (other_colon != std::string::npos) {
            other_volume_name = other_volume.substr(0, other_colon);
          }

          if (volume_name == other_volume_name) {
            DetectedDependency d;
            d.service = service_name;
            d.depends_on = other_service_name;
            d.type = "shared_volume";
            d.details = "Both use volume: " + volume_name;
            dependencies.push_back(d);
          }
        }
      }
    }
  }

  if (log_) {
    log_->info("compose_parser",
               "Extracted " + std::to_string(dependencies.size()) + " dependencies");
  }

  return dependencies;
}

} // namespace nazg::docker_monitor

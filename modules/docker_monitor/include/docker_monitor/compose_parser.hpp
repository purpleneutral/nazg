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

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nazg {
namespace blackbox {
class logger;
}
} // namespace nazg

namespace nazg::docker_monitor {

struct ComposeService {
  std::string name;
  std::string image;
  std::string container_name;
  std::vector<std::string> depends_on;
  std::string network_mode;  // e.g., "service:gluetun"
  std::vector<std::string> networks;
  std::map<std::string, std::string> network_static_ips;  // network -> IP
  std::vector<std::string> volumes;
  std::map<std::string, std::string> environment;
  std::string restart_policy;
  bool privileged = false;
};

struct ComposeNetwork {
  std::string name;
  std::string driver;
  bool external = false;
  std::map<std::string, std::string> ipam_config;
};

struct ComposeFile {
  std::string version;
  std::map<std::string, ComposeService> services;
  std::map<std::string, ComposeNetwork> networks;
  std::map<std::string, std::string> volumes;
};

// Dependency relationship discovered from compose file
struct DetectedDependency {
  std::string service;
  std::string depends_on;
  std::string type;  // depends_on, network_mode, shared_network, volume
  std::string details;  // Additional context
};

// Docker Compose file parser
// Handles YAML parsing and dependency extraction
class ComposeParser {
public:
  explicit ComposeParser(::nazg::blackbox::logger *log = nullptr);

  // Parse a compose file (YAML content)
  std::optional<ComposeFile> parse(const std::string &yaml_content);

  // Parse from file path
  std::optional<ComposeFile> parse_file(const std::string &file_path);

  // Extract all dependencies from parsed compose file
  std::vector<DetectedDependency> extract_dependencies(const ComposeFile &compose);

  // Get last parse error
  std::string last_error() const { return last_error_; }

private:
  ::nazg::blackbox::logger *log_ = nullptr;
  std::string last_error_;

  // Simple YAML parsing helpers (not full YAML spec, focused on compose)
  std::string trim(const std::string &str);
  int get_indent_level(const std::string &line);
  std::string extract_key(const std::string &line);
  std::string extract_value(const std::string &line);
  bool is_list_item(const std::string &line);
  std::string extract_list_item(const std::string &line);

  // Parse specific sections
  void parse_service(const std::vector<std::string> &lines, size_t &pos,
                    const std::string &service_name, ComposeService &service);
  void parse_service_networks(const std::vector<std::string> &lines, size_t &pos,
                              ComposeService &service);
  void parse_networks_section(const std::vector<std::string> &lines, size_t &pos,
                              std::map<std::string, ComposeNetwork> &networks);
};

} // namespace nazg::docker_monitor

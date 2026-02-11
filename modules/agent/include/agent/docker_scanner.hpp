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

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace nazg::blackbox {
class logger;
}

namespace nazg::agent {

struct ContainerInfo {
  std::string id;          // 12-char short ID
  std::string name;
  std::string image;
  std::string state;       // running, exited, paused, etc.
  std::string status;      // "Up 3 days", etc.
  int64_t created;         // Unix timestamp
  std::string ports_json;  // JSON array
  std::string volumes_json; // JSON array
  std::string networks_json; // JSON array
  std::string service_name;
  std::string labels_json;
  std::string health_status;
  std::string restart_policy;
};

struct ComposeFileInfo {
  std::string path;
  std::string project_name;
  std::string services_json; // JSON array of service names
  std::string file_hash;
};

struct DockerImageInfo {
  std::string id;
  std::string repository;
  std::string tag;
  int64_t size_bytes;
  int64_t created;
};

struct DockerNetworkInfo {
  std::string id;
  std::string name;
  std::string driver;
  std::string scope;
};

struct DockerVolumeInfo {
  std::string name;
  std::string driver;
  std::string mountpoint;
};

struct SystemInfo {
  std::string hostname;
  std::string os;
  std::string arch;
  std::string docker_version;
  std::string compose_version;
};

class DockerScanner {
public:
  explicit DockerScanner(::nazg::blackbox::logger *log = nullptr);

  // System information
  SystemInfo get_system_info();

  // Container operations
  std::vector<ContainerInfo> list_containers();
  ContainerInfo inspect_container(const std::string &name_or_id);

  // Image operations
  std::vector<DockerImageInfo> list_images();

  // Network operations
  std::vector<DockerNetworkInfo> list_networks();

  // Volume operations
  std::vector<DockerVolumeInfo> list_volumes();

  // Compose file discovery
  std::vector<ComposeFileInfo> discover_compose_files(
      const std::vector<std::string> &search_paths = {"/opt", "/srv", "/home"});

  // Generate full scan JSON payload
  std::string generate_full_scan_json(const std::string &server_label);

  // Check if docker is available
  bool is_docker_available();

private:
  ::nazg::blackbox::logger *log_ = nullptr;

  // Helper functions
  std::string exec_command(const std::string &cmd);
  std::string parse_docker_ps_line(const std::string &line, int field);
  std::string calculate_file_hash(const std::string &file_path);
  std::string escape_json(const std::string &str);
};

} // namespace nazg::agent

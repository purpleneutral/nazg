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
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nazg {
namespace blackbox {
class logger;
}
namespace nexus {
class Store;
}
namespace bot {
class AgentTransport;
}
} // namespace nazg

namespace nazg::docker_monitor {

struct ScanResult {
  bool success = false;
  std::string error_message;
  int containers_count = 0;
  int images_count = 0;
  int networks_count = 0;
  int volumes_count = 0;
  int compose_files_count = 0;
};

// Docker monitoring client for control center
// Connects to agents and requests Docker environment scans
class Client {
public:
  explicit Client(::nazg::nexus::Store *store,
                 ::nazg::blackbox::logger *log = nullptr);
  ~Client();

  // Scan a server's Docker environment
  // Connects to the agent, requests full scan, and stores results
  ScanResult scan_server(const std::string &server_label);

  // Register an agent (request agent info and store capabilities)
  bool register_agent(const std::string &server_label);

  // Get scan statistics
  std::optional<std::string> get_scan_stats(const std::string &server_label);

private:
  ::nazg::nexus::Store *store_;
  ::nazg::blackbox::logger *log_ = nullptr;

  // Connect to agent and send message
  bool send_message_to_agent(const ::nazg::bot::HostConfig &host,
                             uint8_t message_type,
                             const std::string &payload,
                             std::string &response);

  // Parse and store Docker scan JSON
  bool parse_and_store_scan(int64_t server_id,
                            const std::string &server_label,
                            const std::string &json_payload,
                            ScanResult &result);

  // Parse and store registration response
  bool parse_and_store_registration(int64_t server_id,
                                     const std::string &json_payload);
};

} // namespace nazg::docker_monitor

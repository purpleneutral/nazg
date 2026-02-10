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

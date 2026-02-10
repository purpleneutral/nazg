#pragma once

#include <cstdint>
#include <optional>
#include <string>


namespace nazg::directive {
class registry;
struct context;
}

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::docker_monitor {

struct HelloWorldResult {
  bool success = false;
  int exit_code = -1;
  std::string output;
  std::string error;
};

HelloWorldResult run_hello_world_container(const std::string& server_label,
                                           nazg::nexus::Store* store,
                                           nazg::blackbox::logger* log = nullptr,
                                           bool pull_first = false);

HelloWorldResult run_hello_world_container(int64_t server_id,
                                           nazg::nexus::Store* store,
                                           nazg::blackbox::logger* log = nullptr,
                                           bool pull_first = false);

/**
 * @brief Register Docker CLI commands with the directive system
 *
 * Registers commands for:
 * - Stack management (create, list, show, restart, delete)
 * - Container control (start, stop, restart, logs)
 * - Dependency management (show dependencies, calculate start order)
 * - Server management (list, scan, status)
 */
void register_commands(directive::registry& reg, const directive::context& ctx);

} // namespace nazg::docker_monitor

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

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

#include "docker_monitor/commands.hpp"
#include "docker_monitor/client.hpp"
#include "docker_monitor/orchestrator.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"
#include "nexus/store.hpp"
#include "blackbox/logger.hpp"
#include <string>
#include <string_view>
#include <optional>
#include <cstdio>
#include <array>
#ifndef _WIN32
#include <sys/wait.h>
#endif
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace nazg::docker_monitor {

namespace {

// Helper to parse server argument
std::optional<int64_t> get_server_id(const std::string& server_label, nazg::nexus::Store* store) {
  if (!store) return std::nullopt;
  return store->get_server_id(server_label);
}

HelloWorldResult make_error_result(const std::string& message) {
  HelloWorldResult result;
  result.error = message;
  return result;
}

bool is_local_host(std::string_view host) {
  return host.empty() || host == "localhost" || host == "127.0.0.1" ||
         host == "::1" || host == "0.0.0.0";
}

HelloWorldResult execute_hello_world(bool pull_first,
                                     nazg::blackbox::logger* log) {
  std::string command = pull_first
    ? "docker pull hello-world && docker run --rm hello-world"
    : "docker run --rm hello-world";
  command += " 2>&1";

  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    if (log) {
      log->error("docker_monitor", "Failed to start docker command: " + command);
    }
    return make_error_result("Failed to execute docker command");
  }

  std::string output;
  std::array<char, 256> buffer{};
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    output.append(buffer.data());
  }

  int status = pclose(pipe);
  HelloWorldResult result;
  result.output = output;
  if (status == -1) {
    result.exit_code = -1;
    result.success = false;
    result.error = output.empty() ? "docker command failed" : output;
    return result;
  }

#ifdef _WIN32
  result.exit_code = status;
#else
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else {
    result.exit_code = status;
  }
#endif

  result.success = (result.exit_code == 0);
  if (!result.success) {
    result.error = output.empty()
      ? "docker run hello-world failed"
      : output;
  }
  return result;
}

HelloWorldResult run_local_hello_world(const std::string& server_label,
                                       std::string_view host,
                                       bool pull_first,
                                       nazg::blackbox::logger* log) {
  if (!is_local_host(host)) {
    std::string msg = "Running hello-world is only supported for local hosts (";
    msg += "server '" + server_label + "' points to '" + std::string(host) + "')";
    if (log) {
      log->warn("docker_monitor", msg);
    }
    return make_error_result(msg);
  }

  auto result = execute_hello_world(pull_first, log);
  if (log) {
    if (result.success) {
      log->info("docker_monitor", "hello-world container executed successfully on '" + server_label + "'");
    } else {
      log->warn("docker_monitor", "hello-world container failed on '" + server_label + "' (exit=" + std::to_string(result.exit_code) + ")");
    }
  }
  return result;
}

// ===== Server Commands =====

int cmd_docker_server_list(const directive::command_context& cctx, const directive::context& ectx) {
  (void)cctx; // No args needed for server list

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  auto servers = ectx.store->list_servers();

  if (servers.empty()) {
    std::cout << "No servers configured.\n";
    std::cout << "\nAdd a server with: nazg docker server add <label> <host>\n";
    return 0;
  }

  std::cout << "\nConfigured Docker Servers:\n";
  std::cout << std::string(80, '=') << "\n";
  std::cout << std::left << std::setw(20) << "LABEL"
            << std::setw(30) << "HOST"
            << std::setw(15) << "STATUS"
            << std::setw(15) << "CONTAINERS" << "\n";
  std::cout << std::string(80, '-') << "\n";

  for (const auto& server : servers) {
    std::string label = server.at("label");
    std::string host = server.at("host");
    std::string status = server.count("agent_status") ? server.at("agent_status") : "unknown";

    int64_t server_id = std::stoll(server.at("id"));
    auto containers = ectx.store->list_containers(server_id);

    std::cout << std::left << std::setw(20) << label
              << std::setw(30) << host
              << std::setw(15) << status
              << std::setw(15) << containers.size() << "\n";
  }

  std::cout << std::string(80, '=') << "\n\n";
  return 0;
}

int cmd_docker_server_scan(const directive::command_context& cctx, const directive::context& ectx) {
  std::string server_label;

  // Parse arguments (argv: nazg docker server scan <label>)
  // Index 4 is the server label
  for (int i = 4; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg docker server scan <server-label>\n\n";
      std::cout << "Scan a server's Docker environment and update the database.\n\n";
      std::cout << "Arguments:\n";
      std::cout << "  server-label    Label of the server to scan\n";
      return 0;
    } else if (server_label.empty()) {
      server_label = arg;
    }
  }

  if (server_label.empty()) {
    std::cerr << "Error: Server label required\n";
    std::cerr << "Usage: nazg docker server scan <server-label>\n";
    return 1;
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  // Get server ID
  auto server_id = get_server_id(server_label, ectx.store);
  if (!server_id) {
    std::cerr << "Error: Server '" << server_label << "' not found\n";
    return 1;
  }

  std::cout << "Scanning server '" << server_label << "'...\n";

  // TODO: Trigger agent scan via transport
  // For now, just show what would happen
  std::cout << "Note: Agent scanning not yet implemented\n";
  std::cout << "Will send DockerFullScan command to agent on server\n";

  return 0;
}

// ===== Container Commands =====

int cmd_docker_container_list(const directive::command_context& cctx, const directive::context& ectx) {
  std::string server_label;
  bool show_all = false;

  // Parse arguments (argv: nazg docker container list <server> [-a])
  // Index 4 is the server label
  for (int i = 4; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg docker container list [options] <server-label>\n\n";
      std::cout << "List Docker containers on a server.\n\n";
      std::cout << "Arguments:\n";
      std::cout << "  server-label    Label of the server\n\n";
      std::cout << "Options:\n";
      std::cout << "  -a, --all       Show all containers (including stopped)\n";
      return 0;
    } else if (arg == "-a" || arg == "--all") {
      show_all = true;
    } else if (server_label.empty()) {
      server_label = arg;
    }
  }

  if (server_label.empty()) {
    std::cerr << "Error: Server label required\n";
    std::cerr << "Usage: nazg docker container list <server-label>\n";
    return 1;
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  auto server_id = get_server_id(server_label, ectx.store);
  if (!server_id) {
    std::cerr << "Error: Server '" << server_label << "' not found\n";
    return 1;
  }

  auto containers = ectx.store->list_containers(*server_id);

  if (containers.empty()) {
    std::cout << "No containers found on '" << server_label << "'\n";
    return 0;
  }

  std::cout << "\nContainers on '" << server_label << "':\n";
  std::cout << std::string(120, '=') << "\n";
  std::cout << std::left << std::setw(25) << "NAME"
            << std::setw(12) << "STATE"
            << std::setw(35) << "IMAGE"
            << std::setw(15) << "HEALTH"
            << std::setw(33) << "STATUS" << "\n";
  std::cout << std::string(120, '-') << "\n";

  int shown = 0;
  for (const auto& container : containers) {
    std::string state = container.at("state");

    if (!show_all && state != "running") {
      continue;
    }

    std::string name = container.at("name");
    std::string image = container.at("image");
    std::string status = container.at("status");
    std::string health = container.count("health_status") ? container.at("health_status") : "n/a";

    // Truncate long fields
    if (name.length() > 24) name = name.substr(0, 21) + "...";
    if (image.length() > 34) image = image.substr(0, 31) + "...";
    if (status.length() > 32) status = status.substr(0, 29) + "...";

    std::cout << std::left << std::setw(25) << name
              << std::setw(12) << state
              << std::setw(35) << image
              << std::setw(15) << health
              << std::setw(33) << status << "\n";
    shown++;
  }

  std::cout << std::string(120, '=') << "\n";
  std::cout << "Total: " << shown << " containers";
  if (!show_all) {
    std::cout << " (use -a to show all)";
  }
  std::cout << "\n\n";

  return 0;
}

int cmd_docker_container_restart(const directive::command_context& cctx, const directive::context& ectx) {
  std::string server_label;
  std::string container_name;
  bool with_dependents = false;

  // Parse arguments (argv: nazg docker container restart <server> <container>)
  // Index 4 is server, index 5 is container
  for (int i = 4; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg docker container restart [options] <server-label> <container>\n\n";
      std::cout << "Restart a Docker container with dependency awareness.\n\n";
      std::cout << "Arguments:\n";
      std::cout << "  server-label    Label of the server\n";
      std::cout << "  container       Container name or service name\n\n";
      std::cout << "Options:\n";
      std::cout << "  --with-deps     Also restart dependent containers\n\n";
      std::cout << "Examples:\n";
      std::cout << "  nazg docker container restart myserver gluetun\n";
      std::cout << "  nazg docker container restart --with-deps myserver transmission\n";
      return 0;
    } else if (arg == "--with-deps" || arg == "--with-dependents") {
      with_dependents = true;
    } else if (server_label.empty()) {
      server_label = arg;
    } else if (container_name.empty()) {
      container_name = arg;
    }
  }

  if (server_label.empty() || container_name.empty()) {
    std::cerr << "Error: Server label and container name required\n";
    std::cerr << "Usage: nazg docker container restart <server-label> <container>\n";
    return 1;
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  auto server_id = get_server_id(server_label, ectx.store);
  if (!server_id) {
    std::cerr << "Error: Server '" << server_label << "' not found\n";
    return 1;
  }

  std::cout << "Restarting container '" << container_name << "' on '" << server_label << "'";
  if (with_dependents) {
    std::cout << " (with dependents)";
  }
  std::cout << "...\n";

  // TODO: Implement actual restart via orchestrator and agent transport
  std::cout << "\nNote: Container restart not yet implemented\n";
  std::cout << "Will:\n";
  std::cout << "  1. Query dependencies from database\n";
  std::cout << "  2. Calculate proper stop/start order\n";
  std::cout << "  3. Execute restart via agent\n";
  std::cout << "  4. Wait for health checks\n";

  return 0;
}

int cmd_docker_container_hello_world(const directive::command_context& cctx,
                                     const directive::context& ectx) {
  std::string server_label;
  bool pull_first = false;

  for (int i = 4; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << R"(Usage: nazg docker container hello-world [options] <server-label>

Run the docker hello-world container on the target server.

Arguments:
  server-label         Label of the target server

Options:
  --pull, -p           Run 'docker pull hello-world' before execution

Examples:
  nazg docker container hello-world homelab
  nazg docker container hello-world --pull localhost
)";
      return 0;
    }
    if (arg == "--pull" || arg == "-p") {
      pull_first = true;
    } else if (server_label.empty()) {
      server_label = std::move(arg);
    } else {
      std::cerr << "Unexpected argument: " << arg << '\n';
      return 1;
    }
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized" << '\n';
    return 1;
  }

  if (server_label.empty()) {
    auto servers = ectx.store->list_servers();
    if (servers.size() == 1 && servers[0].count("label")) {
      server_label = servers[0].at("label");
      if (ectx.log) {
        ectx.log->info("docker_monitor",
                        "Defaulting hello-world target to server '" + server_label + "'");
      }
    }
  }

  if (server_label.empty()) {
    std::cerr << "Error: Server label required" << '\n';
    std::cerr << "Usage: nazg docker container hello-world <server-label>" << '\n';
    return 1;
  }

  auto result = run_hello_world_container(server_label, ectx.store, ectx.log, pull_first);

  if (!result.output.empty()) {
    std::cout << result.output;
    if (result.output.back() != '\n') {
      std::cout << '\n';
    }
  }

  if (!result.success) {
    std::string msg = result.error.empty()
        ? "docker run hello-world failed with exit code " + std::to_string(result.exit_code)
        : result.error;
    std::cerr << "Error: " << msg << '\n';
    return 1;
  }

  if (result.output.empty()) {
    std::cout << "hello-world completed successfully on '" << server_label << "'" << '\n';
  }

  return 0;
}

// ===== Stack Commands =====

int cmd_docker_stack_create(const directive::command_context& cctx, const directive::context& ectx) {
  std::string server_label;
  std::string stack_name;
  std::vector<std::string> compose_files;
  std::vector<std::string> env_files;
  std::string description;

  // Parse arguments (argv: nazg docker stack create <server> <stack-name> -f ...)
  // Index 4 is server, index 5 is stack name
  for (int i = 4; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg docker stack create [options] <server-label> <stack-name>\n\n";
      std::cout << "Create a Docker stack from compose files.\n\n";
      std::cout << "Arguments:\n";
      std::cout << "  server-label      Label of the server\n";
      std::cout << "  stack-name        Name for the new stack\n\n";
      std::cout << "Options:\n";
      std::cout << "  -f, --file PATH   Compose file to include (can be repeated)\n";
      std::cout << "  -e, --env PATH    Environment file (can be repeated)\n";
      std::cout << "  -d, --desc TEXT   Stack description\n\n";
      std::cout << "Examples:\n";
      std::cout << "  nazg docker stack create myserver vpn-stack \\\n";
      std::cout << "    -f /opt/docker/gluetun.yml \\\n";
      std::cout << "    -f /opt/docker/media.yml \\\n";
      std::cout << "    -d \"VPN-routed media stack\"\n";
      return 0;
    } else if (arg == "-f" || arg == "--file") {
      if (i + 1 < cctx.argc) {
        compose_files.push_back(cctx.argv[++i]);
      }
    } else if (arg == "-e" || arg == "--env") {
      if (i + 1 < cctx.argc) {
        env_files.push_back(cctx.argv[++i]);
      }
    } else if (arg == "-d" || arg == "--desc" || arg == "--description") {
      if (i + 1 < cctx.argc) {
        description = cctx.argv[++i];
      }
    } else if (server_label.empty()) {
      server_label = arg;
    } else if (stack_name.empty()) {
      stack_name = arg;
    }
  }

  if (server_label.empty() || stack_name.empty()) {
    std::cerr << "Error: Server label and stack name required\n";
    std::cerr << "Usage: nazg docker stack create <server-label> <stack-name> -f <compose-file>\n";
    return 1;
  }

  if (compose_files.empty()) {
    std::cerr << "Error: At least one compose file required (-f option)\n";
    return 1;
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  auto server_id = get_server_id(server_label, ectx.store);
  if (!server_id) {
    std::cerr << "Error: Server '" << server_label << "' not found\n";
    return 1;
  }

  std::cout << "Creating stack '" << stack_name << "' on '" << server_label << "'...\n";
  std::cout << "\nCompose files:\n";
  for (const auto& file : compose_files) {
    std::cout << "  - " << file << "\n";
  }

  if (!env_files.empty()) {
    std::cout << "\nEnvironment files:\n";
    for (const auto& file : env_files) {
      std::cout << "  - " << file << "\n";
    }
  }

  // Create orchestrator
  Orchestrator orch(ectx.store, ectx.log);

  try {
    int64_t stack_id = orch.create_stack_from_compose(
      *server_id, stack_name, compose_files, env_files, description
    );

    std::cout << "\n✓ Stack created successfully (ID: " << stack_id << ")\n";
    std::cout << "\nNazg has automatically:\n";
    std::cout << "  ✓ Parsed all compose files\n";
    std::cout << "  ✓ Detected service dependencies\n";
    std::cout << "  ✓ Identified network mode dependencies\n";
    std::cout << "  ✓ Mapped shared networks and volumes\n";
    std::cout << "  ✓ Stored dependency graph in database\n";
    std::cout << "\nView dependencies: nazg docker stack deps " << server_label << " " << stack_name << "\n";
    std::cout << "Restart stack:     nazg docker stack restart " << server_label << " " << stack_name << "\n\n";

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "\nError creating stack: " << e.what() << "\n";
    return 1;
  }
}

int cmd_docker_stack_list(const directive::command_context& cctx, const directive::context& ectx) {
  std::string server_label;

  // Parse arguments (argv: nazg docker stack list <server>)
  // Index 4 is server label
  for (int i = 4; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg docker stack list <server-label>\n\n";
      std::cout << "List all Docker stacks on a server.\n\n";
      std::cout << "Arguments:\n";
      std::cout << "  server-label    Label of the server\n";
      return 0;
    } else if (server_label.empty()) {
      server_label = arg;
    }
  }

  if (server_label.empty()) {
    std::cerr << "Error: Server label required\n";
    std::cerr << "Usage: nazg docker stack list <server-label>\n";
    return 1;
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  auto server_id = get_server_id(server_label, ectx.store);
  if (!server_id) {
    std::cerr << "Error: Server '" << server_label << "' not found\n";
    return 1;
  }

  Orchestrator orch(ectx.store, ectx.log);
  auto stacks = orch.list_stacks(*server_id);

  if (stacks.empty()) {
    std::cout << "No stacks configured on '" << server_label << "'\n";
    std::cout << "\nCreate a stack with: nazg docker stack create\n";
    return 0;
  }

  std::cout << "\nStacks on '" << server_label << "':\n";
  std::cout << std::string(100, '=') << "\n";
  std::cout << std::left << std::setw(25) << "NAME"
            << std::setw(15) << "COMPOSE FILES"
            << std::setw(60) << "DESCRIPTION" << "\n";
  std::cout << std::string(100, '-') << "\n";

  for (const auto& stack : stacks) {
    std::string name = stack.name;
    std::string desc = stack.description;
    int file_count = static_cast<int>(stack.compose_files.size());

    if (desc.length() > 58) desc = desc.substr(0, 55) + "...";

    std::cout << std::left << std::setw(25) << name
              << std::setw(15) << file_count
              << std::setw(60) << desc << "\n";
  }

  std::cout << std::string(100, '=') << "\n\n";
  return 0;
}

int cmd_docker_stack_show(const directive::command_context& cctx, const directive::context& ectx) {
  std::string server_label;
  std::string stack_name;

  // Parse arguments (argv: nazg docker stack show <server> <stack>)
  // Index 4 is server, index 5 is stack name
  for (int i = 4; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg docker stack show <server-label> <stack-name>\n\n";
      std::cout << "Show detailed information about a stack.\n\n";
      std::cout << "Arguments:\n";
      std::cout << "  server-label    Label of the server\n";
      std::cout << "  stack-name      Name of the stack\n";
      return 0;
    } else if (server_label.empty()) {
      server_label = arg;
    } else if (stack_name.empty()) {
      stack_name = arg;
    }
  }

  if (server_label.empty() || stack_name.empty()) {
    std::cerr << "Error: Server label and stack name required\n";
    std::cerr << "Usage: nazg docker stack show <server-label> <stack-name>\n";
    return 1;
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  auto server_id = get_server_id(server_label, ectx.store);
  if (!server_id) {
    std::cerr << "Error: Server '" << server_label << "' not found\n";
    return 1;
  }

  Orchestrator orch(ectx.store, ectx.log);
  auto stack = orch.get_stack(*server_id, stack_name);

  if (!stack) {
    std::cerr << "Error: Stack '" << stack_name << "' not found on '" << server_label << "'\n";
    return 1;
  }

  std::cout << "\nStack: " << stack->name << "\n";
  std::cout << std::string(80, '=') << "\n";
  std::cout << "ID:          " << stack->id << "\n";
  std::cout << "Server ID:   " << stack->server_id << "\n";
  std::cout << "Description: " << stack->description << "\n";
  std::cout << "Priority:    " << stack->priority << "\n";
  std::cout << "Auto-restart: " << (stack->auto_restart ? "yes" : "no") << "\n";
  std::cout << "Health timeout: " << stack->health_check_timeout << "s\n";

  std::cout << "\nCompose Files:\n";
  for (size_t i = 0; i < stack->compose_files.size(); ++i) {
    std::cout << "  " << (i + 1) << ". " << stack->compose_files[i];
    if (i < stack->env_files.size() && !stack->env_files[i].empty()) {
      std::cout << " (env: " << stack->env_files[i] << ")";
    }
    std::cout << "\n";
  }

  std::cout << "\n";
  return 0;
}

int cmd_docker_stack_deps(const directive::command_context& cctx, const directive::context& ectx) {
  std::string server_label;
  std::string service_name;

  // Parse arguments (argv: nazg docker stack deps <server> <service>)
  // Index 4 is server, index 5 is service
  for (int i = 4; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg docker stack deps <server-label> <service-name>\n\n";
      std::cout << "Show dependencies for a service.\n\n";
      std::cout << "Arguments:\n";
      std::cout << "  server-label    Label of the server\n";
      std::cout << "  service-name    Name of the service\n\n";
      std::cout << "Examples:\n";
      std::cout << "  nazg docker stack deps myserver gluetun\n";
      std::cout << "  nazg docker stack deps myserver transmission\n";
      return 0;
    } else if (server_label.empty()) {
      server_label = arg;
    } else if (service_name.empty()) {
      service_name = arg;
    }
  }

  if (server_label.empty() || service_name.empty()) {
    std::cerr << "Error: Server label and service name required\n";
    std::cerr << "Usage: nazg docker stack deps <server-label> <service-name>\n";
    return 1;
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  auto server_id = get_server_id(server_label, ectx.store);
  if (!server_id) {
    std::cerr << "Error: Server '" << server_label << "' not found\n";
    return 1;
  }

  Orchestrator orch(ectx.store, ectx.log);
  auto deps = orch.get_dependencies(*server_id, service_name);

  std::cout << "\nDependencies for '" << service_name << "':\n";
  std::cout << std::string(80, '=') << "\n";

  if (deps.empty()) {
    std::cout << "No dependencies found.\n";
    std::cout << "\n(This service is at the root of the dependency graph)\n";
  } else {
    std::cout << std::left << std::setw(30) << "DEPENDS ON"
              << std::setw(20) << "TYPE"
              << "\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& dep : deps) {
      std::cout << std::left << std::setw(30) << dep.depends_on
                << std::setw(20) << dep.type
                << "\n";
    }
  }

  std::cout << "\n";
  return 0;
}

int cmd_docker_stack_restart(const directive::command_context& cctx, const directive::context& ectx) {
  std::string server_label;
  std::string stack_name;
  bool wait_health = true;

  // Parse arguments (argv: nazg docker stack restart <server> <stack>)
  // Index 4 is server, index 5 is stack name
  for (int i = 4; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg docker stack restart [options] <server-label> <stack-name>\n\n";
      std::cout << "Intelligently restart a Docker stack respecting dependencies.\n\n";
      std::cout << "Arguments:\n";
      std::cout << "  server-label    Label of the server\n";
      std::cout << "  stack-name      Name of the stack\n\n";
      std::cout << "Options:\n";
      std::cout << "  --no-health     Don't wait for health checks\n\n";
      std::cout << "Example:\n";
      std::cout << "  nazg docker stack restart myserver vpn-stack\n";
      return 0;
    } else if (arg == "--no-health") {
      wait_health = false;
    } else if (server_label.empty()) {
      server_label = arg;
    } else if (stack_name.empty()) {
      stack_name = arg;
    }
  }

  if (server_label.empty() || stack_name.empty()) {
    std::cerr << "Error: Server label and stack name required\n";
    std::cerr << "Usage: nazg docker stack restart <server-label> <stack-name>\n";
    return 1;
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  auto server_id = get_server_id(server_label, ectx.store);
  if (!server_id) {
    std::cerr << "Error: Server '" << server_label << "' not found\n";
    return 1;
  }

  Orchestrator orch(ectx.store, ectx.log);
  auto stack = orch.get_stack(*server_id, stack_name);

  if (!stack) {
    std::cerr << "Error: Stack '" << stack_name << "' not found\n";
    return 1;
  }

  std::cout << "Restarting stack '" << stack_name << "' with intelligent dependency ordering...\n";

  // TODO: Implement actual restart
  std::cout << "\nNote: Stack restart not yet implemented\n";
  std::cout << "Will:\n";
  std::cout << "  1. Calculate dependency graph for all services in stack\n";
  std::cout << "  2. Determine proper stop order (reverse dependencies)\n";
  std::cout << "  3. Stop all services in correct order\n";
  std::cout << "  4. Start all services in dependency order\n";
  if (wait_health) {
    std::cout << "  5. Wait for each service to become healthy before starting next\n";
  }
  std::cout << "  6. Verify all services are running and healthy\n";

  return 0;
}

// ===== Top-level dispatcher =====

void print_docker_help() {
  std::cout << "Usage: nazg docker <command> [options]\n\n";
  std::cout << "Docker orchestration and management with intelligent dependency handling.\n\n";
  std::cout << "Server Management:\n";
  std::cout << "  server list                  List configured Docker servers\n";
  std::cout << "  server scan <server>         Scan a server's Docker environment\n\n";
  std::cout << "Container Management:\n";
  std::cout << "  container list <server> [-a] List containers on a server\n";
  std::cout << "  container restart <server> <name> [--with-deps]\n";
  std::cout << "                               Restart container with dependency awareness\n";
  std::cout << "  container hello-world <server> [--pull]\n";
  std::cout << "                               Run the docker hello-world smoke test\n\n";
  std::cout << "Stack Management (Compose Intelligence):\n";
  std::cout << "  stack create <server> <name> -f <compose> [-f ...] [-d desc]\n";
  std::cout << "                               Create stack from compose files\n";
  std::cout << "  stack list <server>          List stacks on a server\n";
  std::cout << "  stack show <server> <name>   Show stack details\n";
  std::cout << "  stack deps <server> <service> Show service dependencies\n";
  std::cout << "  stack restart <server> <name> Intelligently restart a stack\n\n";
  std::cout << "Examples:\n";
  std::cout << "  nazg docker server list\n";
  std::cout << "  nazg docker stack create myserver vpn-stack -f gluetun.yml -f media.yml\n";
  std::cout << "  nazg docker container restart myserver gluetun\n\n";
  std::cout << "For help on a specific command: nazg docker <command> --help\n";
}

int cmd_docker(const directive::command_context& cctx, const directive::context& ectx) {
  if (cctx.argc < 3) {
    print_docker_help();
    return 1;
  }

  std::string subcmd = cctx.argv[2];

  // Server commands
  if (subcmd == "server") {
    if (cctx.argc < 4) {
      std::cerr << "Usage: nazg docker server <list|scan> ...\n";
      return 1;
    }
    std::string server_subcmd = cctx.argv[3];
    if (server_subcmd == "list") {
      return cmd_docker_server_list(cctx, ectx);
    } else if (server_subcmd == "scan") {
      return cmd_docker_server_scan(cctx, ectx);
    } else {
      std::cerr << "Unknown server subcommand: " << server_subcmd << "\n";
      return 1;
    }
  }

  // Container commands
  if (subcmd == "container") {
    if (cctx.argc < 4) {
      std::cerr << "Usage: nazg docker container <list|restart|hello-world> ...\n";
      return 1;
    }
    std::string container_subcmd = cctx.argv[3];
    if (container_subcmd == "list") {
      return cmd_docker_container_list(cctx, ectx);
    } else if (container_subcmd == "restart") {
      return cmd_docker_container_restart(cctx, ectx);
    } else if (container_subcmd == "hello-world") {
      return cmd_docker_container_hello_world(cctx, ectx);
    } else {
      std::cerr << "Unknown container subcommand: " << container_subcmd << "\n";
      return 1;
    }
  }

  // Stack commands
  if (subcmd == "stack") {
    if (cctx.argc < 4) {
      std::cerr << "Usage: nazg docker stack <create|list|show|deps|restart> ...\n";
      return 1;
    }
    std::string stack_subcmd = cctx.argv[3];
    if (stack_subcmd == "create") {
      return cmd_docker_stack_create(cctx, ectx);
    } else if (stack_subcmd == "list") {
      return cmd_docker_stack_list(cctx, ectx);
    } else if (stack_subcmd == "show") {
      return cmd_docker_stack_show(cctx, ectx);
    } else if (stack_subcmd == "deps") {
      return cmd_docker_stack_deps(cctx, ectx);
    } else if (stack_subcmd == "restart") {
      return cmd_docker_stack_restart(cctx, ectx);
    } else {
      std::cerr << "Unknown stack subcommand: " << stack_subcmd << "\n";
      return 1;
    }
  }

  std::cerr << "Unknown docker subcommand: " << subcmd << "\n";
  print_docker_help();
  return 1;
}

} // namespace

HelloWorldResult run_hello_world_container(const std::string& server_label,
                                           nazg::nexus::Store* store,
                                           nazg::blackbox::logger* log,
                                           bool pull_first) {
  if (!store) {
    return make_error_result("Database not initialized");
  }

  auto server_id = store->get_server_id(server_label);
  if (!server_id) {
    return make_error_result("Server '" + server_label + "' not found");
  }

  return run_hello_world_container(*server_id, store, log, pull_first);
}

HelloWorldResult run_hello_world_container(int64_t server_id,
                                           nazg::nexus::Store* store,
                                           nazg::blackbox::logger* log,
                                           bool pull_first) {
  if (!store) {
    return make_error_result("Database not initialized");
  }

  auto server_row = store->get_server_by_id(server_id);
  if (!server_row) {
    return make_error_result("Server id " + std::to_string(server_id) + " not found");
  }

  std::string label = server_row->count("label") ? server_row->at("label")
                                                  : std::to_string(server_id);
  std::string host = server_row->count("host") ? server_row->at("host") : std::string{};
  return run_local_hello_world(label, host, pull_first, log);
}

void register_commands(directive::registry& reg, const directive::context& /*ctx*/) {
  // Single top-level docker command that handles all subcommands
  reg.add("docker", "Docker orchestration and intelligent container management", cmd_docker);
}

} // namespace nazg::docker_monitor

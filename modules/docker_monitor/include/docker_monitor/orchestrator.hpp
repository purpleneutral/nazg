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
#include <map>
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
} // namespace nazg

namespace nazg::docker_monitor {

struct ServiceDependency {
  std::string service_name;
  std::string depends_on;
  std::string type;  // soft, hard, network, volume
};

struct StackProfile {
  int64_t id = 0;
  int64_t server_id = 0;
  std::string name;
  std::string description;
  int priority = 0;
  bool auto_restart = false;
  int health_check_timeout = 30;
  std::vector<std::string> compose_files;
  std::vector<std::string> env_files;
};

struct OrchestrationRule {
  int64_t id = 0;
  std::string rule_type;
  std::string condition_json;
  std::string action_json;
  int priority = 0;
  bool enabled = true;
  std::string description;
};

struct Issue {
  int64_t id = 0;
  std::string container_id;
  std::string issue_type;
  std::string severity;
  std::string title;
  std::string description;
  int64_t detected_at = 0;
  bool resolved = false;
};

// Docker stack orchestrator - intelligent management of complex stacks
class Orchestrator {
public:
  explicit Orchestrator(::nazg::nexus::Store *store,
                       ::nazg::blackbox::logger *log = nullptr);
  ~Orchestrator();

  // ===== Stack Management =====

  // Create a stack profile
  int64_t create_stack(int64_t server_id, const std::string &name,
                      const std::string &description, int priority = 0);

  // Create stack from one or more compose files
  // Automatically parses dependencies and sets up orchestration
  int64_t create_stack_from_compose(int64_t server_id,
                                    const std::string &stack_name,
                                    const std::vector<std::string> &compose_paths,
                                    const std::vector<std::string> &env_files = {},
                                    const std::string &description = "");

  // Add compose file to stack
  bool add_compose_to_stack(int64_t stack_id, const std::string &compose_path,
                           int execution_order = 0,
                           const std::string &env_file = "");

  // Get stack by name
  std::optional<StackProfile> get_stack(int64_t server_id,
                                        const std::string &stack_name);

  // List all stacks for a server
  std::vector<StackProfile> list_stacks(int64_t server_id);

  // Delete a stack
  bool delete_stack(int64_t stack_id);

  // ===== Dependency Management =====

  // Parse compose file and extract dependencies
  // Returns number of dependencies found
  int parse_compose_dependencies(int64_t server_id,
                                const std::string &compose_path,
                                const std::string &compose_content);

  // Add manual dependency
  bool add_dependency(int64_t server_id, const std::string &service,
                     const std::string &depends_on, const std::string &type = "soft");

  // Get all dependencies for a service
  std::vector<ServiceDependency> get_dependencies(int64_t server_id,
                                                  const std::string &service);

  // Build dependency graph and return start order
  std::vector<std::string> calculate_start_order(int64_t server_id,
                                                 const std::vector<std::string> &services);

  // ===== Orchestration Rules =====

  // Add an orchestration rule
  int64_t add_rule(int64_t server_id, int64_t stack_id,
                  const std::string &rule_type,
                  const std::string &condition_json,
                  const std::string &action_json,
                  int priority = 0,
                  const std::string &description = "");

  // Get all rules for a server/stack
  std::vector<OrchestrationRule> get_rules(int64_t server_id,
                                          int64_t stack_id = 0);

  // Evaluate rules and determine actions
  std::vector<std::string> evaluate_rules(int64_t server_id,
                                          const std::map<std::string, std::string> &context);

  // ===== Issue Detection & Reporting =====

  // Report an issue from agent
  int64_t report_issue(int64_t server_id, const std::string &container_id,
                      const std::string &issue_type, const std::string &severity,
                      const std::string &title, const std::string &description);

  // Get unresolved issues
  std::vector<Issue> get_unresolved_issues(int64_t server_id);

  // Resolve an issue
  bool resolve_issue(int64_t issue_id, const std::string &resolution_action);

  // Auto-resolve issues based on rules
  int auto_resolve_issues(int64_t server_id);

  // ===== Smart Actions =====

  // Restart stack with proper dependency ordering
  bool restart_stack(int64_t stack_id, bool wait_for_health = true);

  // Restart service and its dependents
  bool restart_service(int64_t server_id, const std::string &service_name,
                      bool restart_dependents = false);

  // Start stack in correct order
  bool start_stack(int64_t stack_id);

  // Stop stack in reverse dependency order
  bool stop_stack(int64_t stack_id);

  // Fix a specific issue
  bool fix_issue(int64_t issue_id);

  // ===== Action Logging =====

  int64_t log_action(int64_t server_id, int64_t stack_id,
                    const std::string &action_type, const std::string &target,
                    const std::string &reason, const std::string &triggered_by = "manual");

  bool complete_action(int64_t action_id, bool success, int exit_code = 0,
                      const std::string &output = "", const std::string &error = "");

private:
  ::nazg::nexus::Store *store_;
  ::nazg::blackbox::logger *log_ = nullptr;

  // Dependency graph building
  std::vector<std::string> topological_sort(const std::map<std::string, std::vector<std::string>> &graph);

  // Check if service is healthy
  bool is_service_healthy(int64_t server_id, const std::string &service_name);

  // Wait for service to become healthy
  bool wait_for_health(int64_t server_id, const std::string &service_name, int timeout_sec);

  // Execute docker compose command
  bool execute_compose_command(int64_t server_id, const std::vector<std::string> &compose_files,
                               const std::string &command, const std::string &service = "");
};

} // namespace nazg::docker_monitor

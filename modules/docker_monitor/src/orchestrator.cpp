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

#include "docker_monitor/orchestrator.hpp"
#include "docker_monitor/compose_parser.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <queue>
#include <set>
#include <sstream>

namespace nazg::docker_monitor {

Orchestrator::Orchestrator(::nazg::nexus::Store *store, ::nazg::blackbox::logger *log)
    : store_(store), log_(log) {}

Orchestrator::~Orchestrator() = default;

// ===== Stack Management =====

int64_t Orchestrator::create_stack(int64_t server_id, const std::string &name,
                                   const std::string &description, int priority) {
  if (!store_) {
    return 0;
  }

  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  std::string sql =
      "INSERT INTO docker_stacks (server_id, name, description, priority, "
      "auto_restart, health_check_timeout, created_at, updated_at) "
      "VALUES (?, ?, ?, ?, 0, 30, ?, ?)";

  bool ok = store_->connection()->execute(sql, {
    std::to_string(server_id),
    name,
    description,
    std::to_string(priority),
    std::to_string(timestamp),
    std::to_string(timestamp)
  });

  if (!ok) {
    if (log_) {
      log_->error("orchestrator", "Failed to create stack: " + name);
    }
    return 0;
  }

  int64_t stack_id = store_->connection()->last_insert_id();

  if (log_) {
    log_->info("orchestrator", "Created stack: " + name + " (ID: " +
               std::to_string(stack_id) + ")");
  }

  return stack_id;
}

int64_t Orchestrator::create_stack_from_compose(
    int64_t server_id,
    const std::string &stack_name,
    const std::vector<std::string> &compose_paths,
    const std::vector<std::string> &env_files,
    const std::string &description) {

  if (!store_) {
    return 0;
  }

  if (log_) {
    log_->info("orchestrator",
               "Creating stack '" + stack_name + "' from " +
               std::to_string(compose_paths.size()) + " compose files");
  }

  // Create the stack
  int64_t stack_id = create_stack(server_id, stack_name, description, 0);
  if (stack_id == 0) {
    if (log_) {
      log_->error("orchestrator", "Failed to create stack: " + stack_name);
    }
    return 0;
  }

  ComposeParser parser(log_);
  int total_dependencies = 0;

  // Process each compose file
  for (size_t i = 0; i < compose_paths.size(); i++) {
    const auto &compose_path = compose_paths[i];
    std::string env_file = i < env_files.size() ? env_files[i] : "";

    if (log_) {
      log_->info("orchestrator", "Processing compose file: " + compose_path);
    }

    // Parse the compose file
    auto compose_opt = parser.parse_file(compose_path);
    if (!compose_opt.has_value()) {
      if (log_) {
        log_->error("orchestrator",
                   "Failed to parse compose file: " + compose_path + " - " +
                   parser.last_error());
      }
      continue;
    }

    auto compose = compose_opt.value();

    // Extract and store dependencies
    auto dependencies = parser.extract_dependencies(compose);

    if (log_) {
      log_->info("orchestrator",
                 "Found " + std::to_string(dependencies.size()) +
                 " dependencies in " + compose_path);
    }

    for (const auto &dep : dependencies) {
      if (add_dependency(server_id, dep.service, dep.depends_on, dep.type)) {
        total_dependencies++;

        if (log_) {
          log_->debug("orchestrator",
                     "  " + dep.service + " → " + dep.depends_on +
                     " (" + dep.type + "): " + dep.details);
        }
      }
    }

    // Store services in compose_files table if not already there
    // First check if compose file exists in DB
    auto check_result = store_->connection()->query(
        "SELECT id FROM compose_files WHERE server_id = ? AND file_path = ?",
        {std::to_string(server_id), compose_path});

    int64_t compose_file_id = 0;
    if (!check_result.rows.empty()) {
      compose_file_id = check_result.rows[0].get_int("id").value_or(0);
    } else {
      // Create compose file entry
      std::vector<std::string> service_names;
      for (const auto &[svc_name, svc] : compose.services) {
        service_names.push_back(svc_name);
      }

      std::ostringstream services_json;
      services_json << "[";
      for (size_t j = 0; j < service_names.size(); j++) {
        if (j > 0) services_json << ",";
        services_json << "\"" << service_names[j] << "\"";
      }
      services_json << "]";

      compose_file_id = store_->upsert_compose_file(
          server_id,
          compose_path,
          "",  // project_name - could extract from compose file
          services_json.str(),
          "[]",  // networks_json
          "[]",  // volumes_json
          ""     // file_hash
      );
    }

    // Store network dependencies with static IPs
    for (const auto &[svc_name, svc] : compose.services) {
      for (const auto &[network, ip] : svc.network_static_ips) {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                           now.time_since_epoch()).count();

        store_->connection()->execute(
            "INSERT OR REPLACE INTO docker_network_dependencies "
            "(server_id, service_name, network_name, static_ip, created_at) "
            "VALUES (?, ?, ?, ?, ?)",
            {std::to_string(server_id), svc_name, network, ip,
             std::to_string(timestamp)});

        if (log_) {
          log_->debug("orchestrator",
                     "  Network: " + svc_name + " → " + network +
                     " (static IP: " + ip + ")");
        }
      }
    }

    // Add compose file to stack
    if (compose_file_id > 0) {
      add_compose_to_stack(stack_id, compose_path, i, env_file);
    }
  }

  if (log_) {
    log_->info("orchestrator",
               "Stack '" + stack_name + "' created with " +
               std::to_string(total_dependencies) + " dependencies");
  }

  return stack_id;
}

bool Orchestrator::add_compose_to_stack(int64_t stack_id,
                                        const std::string &compose_path,
                                        int execution_order,
                                        const std::string &env_file) {
  if (!store_) {
    return false;
  }

  // Get or create compose_file entry
  auto result = store_->connection()->query(
      "SELECT id FROM compose_files WHERE file_path = ?",
      {compose_path});

  int64_t compose_file_id = 0;
  if (!result.rows.empty()) {
    compose_file_id = result.rows[0].get_int("id").value_or(0);
  }

  if (compose_file_id == 0) {
    if (log_) {
      log_->warn("orchestrator", "Compose file not found in database: " + compose_path);
      log_->info("orchestrator", "Run a Docker scan to import compose files first");
    }
    return false;
  }

  // Add to stack
  std::string sql =
      "INSERT OR REPLACE INTO docker_stack_compose_files "
      "(stack_id, compose_file_id, execution_order, env_file, compose_flags) "
      "VALUES (?, ?, ?, ?, '[]')";

  bool ok = store_->connection()->execute(sql, {
    std::to_string(stack_id),
    std::to_string(compose_file_id),
    std::to_string(execution_order),
    env_file
  });

  if (log_ && ok) {
    log_->info("orchestrator", "Added compose file to stack: " + compose_path);
  }

  return ok;
}

std::optional<StackProfile> Orchestrator::get_stack(int64_t server_id,
                                                    const std::string &stack_name) {
  if (!store_) {
    return std::nullopt;
  }

  auto result = store_->connection()->query(
      "SELECT id, server_id, name, description, priority, auto_restart, "
      "health_check_timeout FROM docker_stacks "
      "WHERE server_id = ? AND name = ?",
      {std::to_string(server_id), stack_name});

  if (result.rows.empty()) {
    return std::nullopt;
  }

  StackProfile stack;
  stack.id = result.rows[0].get_int("id").value_or(0);
  stack.server_id = result.rows[0].get_int("server_id").value_or(0);
  stack.name = result.rows[0].get("name").value_or("");
  stack.description = result.rows[0].get("description").value_or("");
  stack.priority = result.rows[0].get_int("priority").value_or(0);
  stack.auto_restart = result.rows[0].get_int("auto_restart").value_or(0) != 0;
  stack.health_check_timeout = result.rows[0].get_int("health_check_timeout").value_or(30);

  // Get compose files
  auto compose_result = store_->connection()->query(
      "SELECT cf.file_path, scf.env_file "
      "FROM docker_stack_compose_files scf "
      "JOIN compose_files cf ON scf.compose_file_id = cf.id "
      "WHERE scf.stack_id = ? "
      "ORDER BY scf.execution_order",
      {std::to_string(stack.id)});

  for (const auto &row : compose_result.rows) {
    stack.compose_files.push_back(row.get("file_path").value_or(""));
    stack.env_files.push_back(row.get("env_file").value_or(""));
  }

  return stack;
}

std::vector<StackProfile> Orchestrator::list_stacks(int64_t server_id) {
  std::vector<StackProfile> stacks;

  if (!store_) {
    return stacks;
  }

  auto result = store_->connection()->query(
      "SELECT id, server_id, name, description, priority, auto_restart, "
      "health_check_timeout FROM docker_stacks "
      "WHERE server_id = ? "
      "ORDER BY priority DESC, name",
      {std::to_string(server_id)});

  for (const auto &row : result.rows) {
    StackProfile stack;
    stack.id = row.get_int("id").value_or(0);
    stack.server_id = row.get_int("server_id").value_or(0);
    stack.name = row.get("name").value_or("");
    stack.description = row.get("description").value_or("");
    stack.priority = row.get_int("priority").value_or(0);
    stack.auto_restart = row.get_int("auto_restart").value_or(0) != 0;
    stack.health_check_timeout = row.get_int("health_check_timeout").value_or(30);

    // Get compose files
    auto compose_result = store_->connection()->query(
        "SELECT cf.file_path, scf.env_file "
        "FROM docker_stack_compose_files scf "
        "JOIN compose_files cf ON scf.compose_file_id = cf.id "
        "WHERE scf.stack_id = ? "
        "ORDER BY scf.execution_order",
        {std::to_string(stack.id)});

    for (const auto &compose_row : compose_result.rows) {
      stack.compose_files.push_back(compose_row.get("file_path").value_or(""));
      stack.env_files.push_back(compose_row.get("env_file").value_or(""));
    }

    stacks.push_back(stack);
  }

  return stacks;
}

bool Orchestrator::delete_stack(int64_t stack_id) {
  if (!store_) {
    return false;
  }

  bool ok = store_->connection()->execute(
      "DELETE FROM docker_stacks WHERE id = ?",
      {std::to_string(stack_id)});

  if (log_ && ok) {
    log_->info("orchestrator", "Deleted stack ID: " + std::to_string(stack_id));
  }

  return ok;
}

// ===== Dependency Management =====

int Orchestrator::parse_compose_dependencies(int64_t server_id,
                                             const std::string &compose_path,
                                             const std::string &compose_content) {
  ComposeParser parser(log_);

  auto compose_opt = parser.parse(compose_content);
  if (!compose_opt.has_value()) {
    if (log_) {
      log_->error("orchestrator", "Failed to parse compose file: " + compose_path);
    }
    return 0;
  }

  auto compose = compose_opt.value();
  auto dependencies = parser.extract_dependencies(compose);

  // Store dependencies in database
  int stored_count = 0;
  for (const auto &dep : dependencies) {
    if (add_dependency(server_id, dep.service, dep.depends_on, dep.type)) {
      stored_count++;
    }
  }

  if (log_) {
    log_->info("orchestrator",
               "Parsed " + std::to_string(stored_count) +
               " dependencies from " + compose_path);
  }

  return stored_count;
}

bool Orchestrator::add_dependency(int64_t server_id, const std::string &service,
                                 const std::string &depends_on, const std::string &type) {
  if (!store_) {
    return false;
  }

  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  std::string sql =
      "INSERT OR REPLACE INTO docker_service_dependencies "
      "(server_id, service_name, depends_on_service, dependency_type, created_at) "
      "VALUES (?, ?, ?, ?, ?)";

  return store_->connection()->execute(sql, {
    std::to_string(server_id),
    service,
    depends_on,
    type,
    std::to_string(timestamp)
  });
}

std::vector<ServiceDependency> Orchestrator::get_dependencies(int64_t server_id,
                                                              const std::string &service) {
  std::vector<ServiceDependency> deps;

  if (!store_) {
    return deps;
  }

  auto result = store_->connection()->query(
      "SELECT service_name, depends_on_service, dependency_type "
      "FROM docker_service_dependencies "
      "WHERE server_id = ? AND service_name = ?",
      {std::to_string(server_id), service});

  for (const auto &row : result.rows) {
    ServiceDependency dep;
    dep.service_name = row.get("service_name").value_or("");
    dep.depends_on = row.get("depends_on_service").value_or("");
    dep.type = row.get("dependency_type").value_or("soft");
    deps.push_back(dep);
  }

  return deps;
}

// Topological sort implementation
std::vector<std::string> Orchestrator::topological_sort(
    const std::map<std::string, std::vector<std::string>> &graph) {

  std::vector<std::string> result;
  std::map<std::string, int> in_degree;
  std::set<std::string> all_nodes;

  // Initialize in-degrees and collect all nodes
  for (const auto &[node, neighbors] : graph) {
    all_nodes.insert(node);
    if (in_degree.find(node) == in_degree.end()) {
      in_degree[node] = 0;
    }

    for (const auto &neighbor : neighbors) {
      all_nodes.insert(neighbor);
      in_degree[neighbor]++;
    }
  }

  // Queue for nodes with no incoming edges
  std::queue<std::string> queue;
  for (const auto &node : all_nodes) {
    if (in_degree[node] == 0) {
      queue.push(node);
    }
  }

  // Kahn's algorithm
  while (!queue.empty()) {
    std::string node = queue.front();
    queue.pop();
    result.push_back(node);

    // Check if this node has outgoing edges
    auto it = graph.find(node);
    if (it != graph.end()) {
      for (const auto &neighbor : it->second) {
        in_degree[neighbor]--;
        if (in_degree[neighbor] == 0) {
          queue.push(neighbor);
        }
      }
    }
  }

  // Check for cycles
  if (result.size() != all_nodes.size()) {
    if (log_) {
      log_->warn("orchestrator", "Dependency graph contains cycles, result may be incomplete");
    }
  }

  return result;
}

std::vector<std::string> Orchestrator::calculate_start_order(
    int64_t server_id,
    const std::vector<std::string> &services) {

  // Build dependency graph
  std::map<std::string, std::vector<std::string>> graph;

  for (const auto &service : services) {
    auto deps = get_dependencies(server_id, service);
    std::vector<std::string> dep_list;

    for (const auto &dep : deps) {
      // Only include hard dependencies and network_mode in start order
      if (dep.type == "depends_on" || dep.type == "network_mode" || dep.type == "hard") {
        dep_list.push_back(dep.depends_on);
      }
    }

    graph[service] = dep_list;
  }

  // Perform topological sort
  auto sorted = topological_sort(graph);

  if (log_) {
    std::ostringstream oss;
    oss << "Start order for " << services.size() << " services: ";
    for (size_t i = 0; i < sorted.size(); i++) {
      if (i > 0) oss << " → ";
      oss << sorted[i];
    }
    log_->info("orchestrator", oss.str());
  }

  return sorted;
}

// ===== Issue Management ===== (stub implementations for now)

int64_t Orchestrator::report_issue(int64_t server_id, const std::string &container_id,
                                   const std::string &issue_type, const std::string &severity,
                                   const std::string &title, const std::string &description) {
  if (!store_) {
    return 0;
  }

  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  std::string sql =
      "INSERT INTO docker_issues "
      "(server_id, container_id, issue_type, severity, title, description, "
      "detected_at, auto_resolve_attempted) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, 0)";

  bool ok = store_->connection()->execute(sql, {
    std::to_string(server_id),
    container_id,
    issue_type,
    severity,
    title,
    description,
    std::to_string(timestamp)
  });

  if (!ok) {
    return 0;
  }

  int64_t issue_id = store_->connection()->last_insert_id();

  if (log_) {
    log_->warn("orchestrator",
               "Reported issue: " + title + " (severity: " + severity + ")");
  }

  return issue_id;
}

std::vector<Issue> Orchestrator::get_unresolved_issues(int64_t server_id) {
  std::vector<Issue> issues;

  if (!store_) {
    return issues;
  }

  auto result = store_->connection()->query(
      "SELECT id, container_id, issue_type, severity, title, description, detected_at "
      "FROM docker_issues "
      "WHERE server_id = ? AND resolved_at IS NULL "
      "ORDER BY detected_at DESC",
      {std::to_string(server_id)});

  for (const auto &row : result.rows) {
    Issue issue;
    issue.id = row.get_int("id").value_or(0);
    issue.container_id = row.get("container_id").value_or("");
    issue.issue_type = row.get("issue_type").value_or("");
    issue.severity = row.get("severity").value_or("medium");
    issue.title = row.get("title").value_or("");
    issue.description = row.get("description").value_or("");
    issue.detected_at = row.get_int("detected_at").value_or(0);
    issue.resolved = false;
    issues.push_back(issue);
  }

  return issues;
}

bool Orchestrator::resolve_issue(int64_t issue_id, const std::string &resolution_action) {
  if (!store_) {
    return false;
  }

  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  std::string sql =
      "UPDATE docker_issues SET resolved_at = ?, resolution_action = ? "
      "WHERE id = ?";

  bool ok = store_->connection()->execute(sql, {
    std::to_string(timestamp),
    resolution_action,
    std::to_string(issue_id)
  });

  if (log_ && ok) {
    log_->info("orchestrator",
               "Resolved issue ID " + std::to_string(issue_id) + ": " + resolution_action);
  }

  return ok;
}

// ===== Action Logging =====

int64_t Orchestrator::log_action(int64_t server_id, int64_t stack_id,
                                 const std::string &action_type,
                                 const std::string &target,
                                 const std::string &reason,
                                 const std::string &triggered_by) {
  if (!store_) {
    return 0;
  }

  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  std::string sql =
      "INSERT INTO docker_orchestration_actions "
      "(server_id, stack_id, action_type, target, reason, triggered_by, "
      "status, started_at) "
      "VALUES (?, ?, ?, ?, ?, ?, 'pending', ?)";

  bool ok = store_->connection()->execute(sql, {
    std::to_string(server_id),
    stack_id > 0 ? std::to_string(stack_id) : "",
    action_type,
    target,
    reason,
    triggered_by,
    std::to_string(timestamp)
  });

  if (!ok) {
    return 0;
  }

  return store_->connection()->last_insert_id();
}

bool Orchestrator::complete_action(int64_t action_id, bool success,
                                   int exit_code, const std::string &output,
                                   const std::string &error) {
  if (!store_) {
    return false;
  }

  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  std::string sql =
      "UPDATE docker_orchestration_actions "
      "SET status = ?, exit_code = ?, output = ?, error = ?, completed_at = ? "
      "WHERE id = ?";

  return store_->connection()->execute(sql, {
    success ? "success" : "failed",
    std::to_string(exit_code),
    output,
    error,
    std::to_string(timestamp),
    std::to_string(action_id)
  });
}

// Stub implementations for now - would need AgentTransport integration
bool Orchestrator::restart_stack(int64_t stack_id, bool /*wait_for_health*/) {
  // TODO: Implement actual restart logic with agent communication
  if (log_) {
    log_->info("orchestrator", "restart_stack called for stack ID: " +
               std::to_string(stack_id));
  }
  return true;
}

bool Orchestrator::restart_service(int64_t /*server_id*/, const std::string &service_name,
                                   bool /*restart_dependents*/) {
  // TODO: Implement
  if (log_) {
    log_->info("orchestrator", "restart_service called for: " + service_name);
  }
  return true;
}

bool Orchestrator::start_stack(int64_t /*stack_id*/) {
  // TODO: Implement
  return true;
}

bool Orchestrator::stop_stack(int64_t /*stack_id*/) {
  // TODO: Implement
  return true;
}

bool Orchestrator::fix_issue(int64_t /*issue_id*/) {
  // TODO: Implement
  return true;
}

int Orchestrator::auto_resolve_issues(int64_t /*server_id*/) {
  // TODO: Implement
  return 0;
}

std::vector<OrchestrationRule> Orchestrator::get_rules(int64_t /*server_id*/, int64_t /*stack_id*/) {
  // TODO: Implement
  return {};
}

std::vector<std::string> Orchestrator::evaluate_rules(
    int64_t /*server_id*/,
    const std::map<std::string, std::string> &/*context*/) {
  // TODO: Implement
  return {};
}

int64_t Orchestrator::add_rule(int64_t /*server_id*/, int64_t /*stack_id*/,
                               const std::string &/*rule_type*/,
                               const std::string &/*condition_json*/,
                               const std::string &/*action_json*/,
                               int /*priority*/,
                               const std::string &/*description*/) {
  // TODO: Implement
  return 0;
}

bool Orchestrator::is_service_healthy(int64_t /*server_id*/, const std::string &/*service_name*/) {
  // TODO: Query container health from database
  return true;
}

bool Orchestrator::wait_for_health(int64_t /*server_id*/, const std::string &/*service_name*/,
                                   int /*timeout_sec*/) {
  // TODO: Implement health check waiting
  return true;
}

bool Orchestrator::execute_compose_command(int64_t /*server_id*/,
                                           const std::vector<std::string> &/*compose_files*/,
                                           const std::string &/*command*/,
                                           const std::string &/*service*/) {
  // TODO: Build and execute docker compose command via agent
  return true;
}

} // namespace nazg::docker_monitor

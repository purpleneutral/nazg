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
#include "nexus/connection.hpp"
#include "nexus/types.hpp"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::nexus {

class Store {
public:
  // Factory: create store with SQLite backend
  static std::unique_ptr<Store> create(const std::string &db_path,
                                       nazg::blackbox::logger *log = nullptr);

  explicit Store(std::unique_ptr<Connection> conn,
                 nazg::blackbox::logger *log = nullptr);
  ~Store();

  // Initialize: run migrations
  bool initialize();
  bool reset_database();

  // Get last initialization error (empty if initialized successfully)
  std::string last_init_error() const { return last_init_error_; }

  // Connection access
  Connection *connection() { return conn_.get(); }

  // ===== Projects =====
  // Get or create project by root path
  int64_t ensure_project(const std::string &root_path);

  std::optional<Project> get_project(int64_t id);
  std::optional<Project> get_project_by_path(const std::string &root);
  void update_project(int64_t id, const Project &proj);
  std::vector<Project> list_projects();

  // ===== Facts =====
  void set_fact(int64_t project_id, const std::string &ns,
                const std::string &key, const std::string &value);

  std::optional<std::string> get_fact(int64_t project_id, const std::string &ns,
                                      const std::string &key);

  std::map<std::string, std::string> list_facts(int64_t project_id,
                                                const std::string &ns);

  // ===== Events =====
  void add_event(int64_t project_id, const std::string &level,
                 const std::string &tag, const std::string &message,
                 const std::string &metadata = "");

  std::vector<Event> recent_events(int64_t project_id, int limit = 50);

  // ===== Command History =====
  void record_command(int64_t project_id, const std::string &cmd,
                      const std::vector<std::string> &args, int exit_code,
                      int64_t duration_ms);

  std::vector<CommandRecord> recent_commands(int limit = 20);
  std::vector<CommandRecord> recent_commands_for_project(int64_t project_id,
                                                          int limit = 20);

  // ===== Snapshots =====
  int64_t add_snapshot(int64_t project_id, const std::string &hash,
                       int file_count, int64_t total_bytes);

  std::optional<Snapshot> latest_snapshot(int64_t project_id);

  // ===== Maintenance =====
  void prune_events(int64_t project_id, int keep_count);
  void prune_commands(int keep_count);

  // ===== Bot Hosts =====
  int64_t add_bot_host(const std::string &label, const std::string &address,
                       const std::string &ssh_config);
  std::optional<int64_t> get_bot_host_id(const std::string &label);
  void update_bot_host_status(int64_t host_id, const std::string &status);
  std::vector<std::map<std::string, std::string>> list_bot_hosts();
  bool update_bot_host(int64_t host_id, const std::string &label,
                       const std::string &address,
                       const std::string &ssh_config);
  bool delete_bot_host(int64_t host_id);

  // ===== Bot Runs =====
  int64_t begin_bot_run(const std::string &bot_name, int64_t host_id);
  void finish_bot_run(int64_t run_id, const std::string &status, int exit_code,
                      int64_t duration_ms);
  std::vector<std::map<std::string, std::string>>
  recent_bot_runs(const std::string &bot_name, int limit = 20);
  std::vector<std::map<std::string, std::string>>
  recent_bot_runs_for_host(int64_t host_id, int limit = 20);

  // ===== Bot Reports =====
  void add_bot_report(int64_t run_id, const std::string &json_payload);
  std::optional<std::string> latest_bot_report(const std::string &bot_name,
                                                int64_t host_id);

  // ===== Test Runs =====
  int64_t add_test_run(int64_t project_id, const std::string &framework,
                       int64_t timestamp, int64_t duration_ms, int exit_code,
                       int total, int passed, int failed, int skipped,
                       int errors, const std::string &triggered_by = "manual");

  std::optional<std::map<std::string, std::string>>
  get_test_run(int64_t run_id);

  std::vector<std::map<std::string, std::string>>
  get_test_runs(int64_t project_id, int limit = 10);

  std::optional<std::map<std::string, std::string>>
  get_latest_test_run(int64_t project_id);

  // ===== Test Results =====
  bool add_test_result(int64_t run_id, const std::string &suite,
                       const std::string &name, const std::string &status,
                       int64_t duration_ms, const std::string &message,
                       const std::string &file, int line);

  std::vector<std::map<std::string, std::string>>
  get_test_results(int64_t run_id);

  std::vector<std::map<std::string, std::string>>
  get_failed_test_results(int64_t run_id);

  // ===== Test Coverage =====
  bool add_test_coverage(int64_t run_id, const std::string &file_path,
                         double line_coverage, double branch_coverage,
                         int lines_covered, int lines_total,
                         int branches_covered, int branches_total);

  std::optional<std::map<std::string, std::string>>
  get_test_coverage_summary(int64_t run_id);

  std::vector<std::map<std::string, std::string>>
  get_test_coverage_files(int64_t run_id);

  // ===== Git Servers =====
  std::optional<GitServer> get_git_server(const std::string &label);
  std::vector<GitServer> list_git_servers();
  bool upsert_git_server(const GitServer &server);
  bool update_git_server_status(const std::string &label,
                                const std::string &status);
  bool mark_git_server_installed(const std::string &label, int64_t timestamp);
  bool update_git_server_last_check(const std::string &label,
                                    int64_t timestamp);
  bool update_git_server_admin_token(const std::string &label,
                                     const std::string &token);
  bool remove_git_server(const std::string &label);

  // ===== Bare Repositories =====
  std::vector<std::string>
  list_bare_repo_paths_with_prefix(const std::string &server_path_prefix);

  // ===== Repo Migrations =====
  int64_t add_repo_migration(const RepoMigrationRecord &record);
  bool update_repo_migration_status(int64_t migration_id,
                                    const std::string &status,
                                    const std::string &error_message = "");
  bool mark_repo_migration_completed(int64_t migration_id, int64_t completed_at);
  std::vector<RepoMigrationRecord>
  list_repo_migrations_for_server(int64_t server_id, int limit = 50);

  // ===== Git Server Health =====
  int64_t add_git_server_health(const GitServerHealthRecord &record);
  std::vector<GitServerHealthRecord>
  list_git_server_health(int64_t server_id, int limit = 20);

  // ===== Docker Monitoring: Servers =====
  int64_t add_server(const std::string &label, const std::string &host,
                     const std::string &ssh_config = "{}");
  std::optional<int64_t> get_server_id(const std::string &label);
  std::optional<std::map<std::string, std::string>>
  get_server(const std::string &label);
  std::optional<std::map<std::string, std::string>>
  get_server_by_id(int64_t server_id);
  std::vector<std::map<std::string, std::string>> list_servers();
  bool update_server_heartbeat(int64_t server_id, const std::string &agent_version,
                                 const std::string &agent_status,
                                 const std::string &capabilities);
  bool update_server_status(int64_t server_id, const std::string &agent_status);
  bool delete_server(int64_t server_id);
  bool update_server_connection(int64_t server_id, const std::string &host,
                                const std::string &ssh_config);
  bool update_server_agent_container(int64_t server_id,
                                     const std::string &strategy,
                                     const std::string &local_tar,
                                     const std::string &remote_tar,
                                     const std::string &image);

  // ===== Docker Monitoring: Containers =====
  int64_t upsert_container(int64_t server_id, const std::string &container_id,
                           const std::string &name, const std::string &image,
                           const std::string &state, const std::string &status,
                           int64_t created, const std::string &ports_json,
                           const std::string &volumes_json,
                           const std::string &networks_json,
                           const std::string &service_name,
                           const std::string &depends_on_json,
                           const std::string &labels_json,
                           const std::string &health_status,
                           const std::string &restart_policy);
  std::vector<std::map<std::string, std::string>>
  list_containers(int64_t server_id = 0);
  std::optional<std::map<std::string, std::string>>
  get_container(int64_t server_id, const std::string &container_name);
  std::optional<std::map<std::string, std::string>>
  get_container_by_id(int64_t server_id, const std::string &container_id);
  bool mark_containers_stale(int64_t server_id, int64_t before_timestamp);
  bool delete_stale_containers(int64_t server_id, int64_t before_timestamp);

  // ===== Docker Monitoring: Compose Files =====
  int64_t upsert_compose_file(int64_t server_id, const std::string &file_path,
                               const std::string &project_name,
                               const std::string &services_json,
                               const std::string &networks_json,
                               const std::string &volumes_json,
                               const std::string &file_hash);
  std::vector<std::map<std::string, std::string>>
  list_compose_files(int64_t server_id = 0);
  std::optional<std::map<std::string, std::string>>
  get_compose_file(int64_t server_id, const std::string &file_path);

  // ===== Docker Monitoring: Images =====
  int64_t upsert_docker_image(int64_t server_id, const std::string &image_id,
                               const std::string &repository, const std::string &tag,
                               int64_t size_bytes, int64_t created);
  std::vector<std::map<std::string, std::string>>
  list_docker_images(int64_t server_id = 0);

  // ===== Docker Monitoring: Networks =====
  int64_t upsert_docker_network(int64_t server_id, const std::string &network_id,
                                 const std::string &name, const std::string &driver,
                                 const std::string &scope,
                                 const std::string &ipam_config_json);
  std::vector<std::map<std::string, std::string>>
  list_docker_networks(int64_t server_id = 0);

  // ===== Docker Monitoring: Volumes =====
  int64_t upsert_docker_volume(int64_t server_id, const std::string &volume_name,
                                const std::string &driver, const std::string &mountpoint,
                                const std::string &labels_json);
  std::vector<std::map<std::string, std::string>>
  list_docker_volumes(int64_t server_id = 0);

  // ===== Docker Monitoring: Status History =====
  int64_t add_docker_status_event(int64_t server_id, const std::string &container_id,
                                   const std::string &container_name,
                                   const std::string &event_type,
                                   const std::string &old_state,
                                   const std::string &new_state,
                                   const std::string &metadata_json);
  std::vector<std::map<std::string, std::string>>
  get_container_history(int64_t server_id, const std::string &container_name,
                        int limit = 50);
  std::vector<std::map<std::string, std::string>>
  get_recent_docker_events(int64_t server_id = 0, int limit = 100);

  // ===== Docker Monitoring: Agent Updates =====
  int64_t log_agent_update(int64_t server_id, const std::string &update_type,
                           const std::string &payload_json);
  bool mark_agent_update_processed(int64_t update_id, bool success,
                                    const std::string &error_message = "");
  std::vector<std::map<std::string, std::string>>
  get_unprocessed_agent_updates(int limit = 100);

  // ===== Docker Monitoring: Commands (for future control operations) =====
  int64_t add_docker_command(int64_t server_id, const std::string &container_id,
                              const std::string &command, const std::string &params_json,
                              const std::string &issued_by);
  bool update_docker_command_status(int64_t command_id, const std::string &status,
                                     int exit_code = -1, const std::string &output = "",
                                     const std::string &error = "");
  std::vector<std::map<std::string, std::string>>
  list_docker_commands(int64_t server_id = 0, int limit = 50);

  // ===== Workspace Snapshots =====
  int64_t add_workspace_snapshot(int64_t project_id,
                                  int64_t brain_snapshot_id,
                                  const std::string &label,
                                  const std::string &trigger_type,
                                  const std::string &build_dir_hash,
                                  const std::string &deps_manifest_hash,
                                  const std::string &env_snapshot_json,
                                  const std::string &system_info_json,
                                  bool is_clean_build,
                                  const std::string &git_commit,
                                  const std::string &git_branch);

  std::optional<std::map<std::string, std::string>>
  get_workspace_snapshot(int64_t snapshot_id);

  std::optional<std::map<std::string, std::string>>
  latest_workspace_snapshot(int64_t project_id);

  std::vector<std::map<std::string, std::string>>
  list_workspace_snapshots(int64_t project_id, int limit = 20);

  bool update_workspace_snapshot_restore_count(int64_t snapshot_id);

  // ===== Workspace Files =====
  void add_workspace_file(int64_t snapshot_id, const std::string &file_path,
                          const std::string &file_type,
                          const std::string &file_hash, int64_t file_size,
                          int64_t mtime);

  std::vector<std::map<std::string, std::string>>
  get_workspace_files(int64_t snapshot_id);

  bool delete_workspace_snapshot(int64_t snapshot_id);

  // ===== Workspace Restores =====
  int64_t begin_workspace_restore(int64_t project_id, int64_t snapshot_id,
                                   const std::string &restore_type,
                                   const std::string &reason);

  void finish_workspace_restore(int64_t restore_id, bool success,
                                 int files_restored, int64_t duration_ms);

  std::vector<std::map<std::string, std::string>>
  list_workspace_restores(int64_t project_id, int limit = 10);

  // ===== Workspace Tags =====
  bool tag_workspace_snapshot(int64_t project_id, int64_t snapshot_id,
                               const std::string &tag_name,
                               const std::string &description = "");

  std::optional<int64_t> get_snapshot_by_tag(int64_t project_id,
                                              const std::string &tag_name);

  bool untag_workspace_snapshot(int64_t project_id,
                                 const std::string &tag_name);

  std::vector<std::map<std::string, std::string>>
  list_workspace_tags(int64_t project_id);

  // ===== Workspace Failures =====
  void record_workspace_failure(int64_t project_id,
                                 const std::string &failure_type,
                                 const std::string &error_signature,
                                 const std::string &error_message,
                                 int64_t before_snapshot_id,
                                 int64_t after_snapshot_id,
                                 const std::string &changed_files_json,
                                 const std::string &changed_deps_json);

  std::vector<std::map<std::string, std::string>>
  find_similar_failures(int64_t project_id, const std::string &error_signature,
                        int limit = 5);

  // ===== Brain Failure Learning =====

  // Enhanced failure recording with full context
  int64_t record_failure_with_context(
      int64_t project_id,
      const std::string &failure_type,
      const std::string &error_signature,
      const std::string &error_message,
      const std::string &error_location,
      const std::string &command_executed,
      int exit_code,
      int64_t before_snapshot_id,
      int64_t after_snapshot_id,
      const std::string &changed_files_json,
      const std::string &changed_deps_json,
      const std::string &changed_env_json,
      const std::string &changed_system_json,
      const std::string &severity = "medium",
      const std::string &tags_json = "[]");

  std::optional<std::map<std::string, std::string>>
  get_failure(int64_t failure_id);

  std::vector<std::map<std::string, std::string>>
  list_failures(int64_t project_id, int limit = 50);

  std::vector<std::map<std::string, std::string>>
  list_unresolved_failures(int64_t project_id, int limit = 50);

  bool update_failure_resolution(
      int64_t failure_id,
      const std::string &resolution_type,
      int64_t resolution_snapshot_id,
      const std::string &notes,
      bool success);

  // ===== Failure Patterns =====

  int64_t record_pattern(
      int64_t project_id,
      const std::string &pattern_signature,
      const std::string &pattern_name,
      const std::string &failure_type,
      const std::string &error_regex,
      const std::string &trigger_conditions_json);

  std::optional<std::map<std::string, std::string>>
  get_pattern_by_signature(int64_t project_id, const std::string &pattern_signature);

  std::optional<std::map<std::string, std::string>>
  get_pattern(int64_t pattern_id);

  std::vector<std::map<std::string, std::string>>
  list_patterns(int64_t project_id, int limit = 100);

  bool update_pattern_statistics(int64_t pattern_id, int occurrence_delta = 1);

  bool link_failure_to_pattern(int64_t failure_id, int64_t pattern_id);

  // ===== Recovery Actions =====

  int64_t add_recovery_action(
      int64_t pattern_id,
      int64_t failure_id,
      const std::string &action_type,
      const std::string &action_params_json,
      const std::string &description,
      bool requires_confirmation = true);

  std::vector<std::map<std::string, std::string>>
  get_recovery_actions_for_pattern(int64_t pattern_id);

  std::vector<std::map<std::string, std::string>>
  get_recovery_actions_for_failure(int64_t failure_id);

  bool update_recovery_action_stats(
      int64_t action_id,
      bool success,
      int64_t execution_time_ms);

  // ===== Recovery History =====

  int64_t begin_recovery_execution(
      int64_t project_id,
      int64_t failure_id,
      int64_t action_id,
      const std::string &execution_mode);

  bool complete_recovery_execution(
      int64_t history_id,
      bool success,
      const std::string &output_log,
      bool verification_passed,
      int64_t execution_time_ms);

  std::vector<std::map<std::string, std::string>>
  get_recovery_history(int64_t failure_id);

  std::vector<std::map<std::string, std::string>>
  get_recent_recoveries(int64_t project_id, int limit = 20);

  // ===== Failure Relationships =====

  bool add_failure_relationship(
      int64_t source_failure_id,
      int64_t related_failure_id,
      const std::string &relationship_type,
      double similarity_score);

  std::vector<std::map<std::string, std::string>>
  get_related_failures(int64_t failure_id, const std::string &relationship_type = "");

private:
  int64_t now_timestamp();

  std::unique_ptr<Connection> conn_;
  nazg::blackbox::logger *log_ = nullptr;
  std::string last_init_error_;

  void log_debug(const std::string &msg) const;
  void log_info(const std::string &msg) const;
  void log_warn(const std::string &msg) const;
  void log_error(const std::string &msg) const;
};

} // namespace nazg::nexus

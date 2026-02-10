#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nazg::nexus {

struct Project {
  int64_t id = 0;
  std::string root_path;
  std::string name;
  std::string language;
  std::string detected_tools; // JSON array
  int64_t created_at = 0;
  int64_t updated_at = 0;
};

struct Snapshot {
  int64_t id = 0;
  int64_t project_id = 0;
  std::string tree_hash;
  int file_count = 0;
  int64_t total_bytes = 0;
  int64_t created_at = 0;
};

struct Event {
  int64_t id = 0;
  int64_t project_id = 0;
  std::string level;    // info, warn, error
  std::string tag;      // scanner, detector, planner
  std::string message;
  std::string metadata; // JSON
  int64_t created_at = 0;
};

struct CommandRecord {
  int64_t id = 0;
  int64_t project_id = 0;
  std::string command;
  std::string args; // JSON array
  int exit_code = 0;
  int64_t duration_ms = 0;
  int64_t executed_at = 0;
};

struct GitServer {
  int64_t id = 0;
  std::string label;
  std::string type;
  std::string host;
  int port = 0;
  int ssh_port = 22;
  std::string ssh_user;
  std::string repo_base_path;
  std::string config_path;
  std::string web_url;
  std::string status;
  int64_t installed_at = 0;
  int64_t last_check = 0;
  std::string config_hash;
  int64_t config_modified = 0;
  int64_t created_at = 0;
  int64_t updated_at = 0;
  std::string admin_token;
};

struct RepoMigrationRecord {
  int64_t id = 0;
  int64_t server_id = 0;
  int64_t project_id = 0;
  std::string repo_name;
  std::string source_path;
  int64_t started_at = 0;
  int64_t completed_at = 0;
  std::string status;
  int branch_count = 0;
  int tag_count = 0;
  int64_t size_bytes = 0;
  std::string error_message;
};

struct GitServerHealthRecord {
  int64_t id = 0;
  int64_t server_id = 0;
  int64_t timestamp = 0;
  std::string status;
  bool web_ui_reachable = false;
  bool http_clone_works = false;
  bool ssh_push_works = false;
  std::string service_status_json;
  int repo_count = 0;
  int64_t total_size_bytes = 0;
  int disk_used_pct = 0;
  int64_t disk_free_bytes = 0;
  std::string notes_json;
};

} // namespace nazg::nexus

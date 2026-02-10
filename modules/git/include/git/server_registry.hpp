#pragma once
#include "git/server.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace nazg::config {
class store;
}

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::prompt {
class Prompt;
}

namespace nazg::git {

// Server entry from config.toml or database
struct ServerEntry {
  int64_t id = 0;
  std::string label;
  ServerConfig config;
  std::string admin_token;

  // Runtime state (from database)
  std::string status;           // 'online', 'offline', 'not_installed', 'installed'
  int64_t installed_at = 0;
  int64_t last_check = 0;
  std::string config_hash;
  int64_t config_modified = 0;

  // Conflict tracking
  bool has_config_changes = false;  // Config differs from database
  std::string config_source;        // 'toml', 'database', 'both'
};

// Manages git servers from config.toml + Nexus database
class ServerRegistry {
public:
  ServerRegistry(config::store* cfg,
                 nexus::Store* store,
                 blackbox::logger* log);

  // Load server by label from config.toml or database
  std::optional<ServerEntry> get_server(const std::string& label);

  // List all known servers (merged from config + database)
  std::vector<ServerEntry> list_servers();

  // Add or update server
  bool add_server(const std::string& label, const ServerConfig& cfg);

  // Update runtime state in database
  bool update_status(const std::string& label, const std::string& status);
  bool mark_installed(const std::string& label);
  bool update_last_check(const std::string& label, int64_t timestamp);
  bool update_admin_token(const std::string& label, const std::string& token);

  // Sync config.toml changes to database with user confirmation
  bool sync_config_to_database(const std::string& label, prompt::Prompt* prompt);

  // Remove server from database (config.toml entries remain)
  bool remove_server(const std::string& label);

  // Repo migration bookkeeping
  bool record_repo_migration_start(const std::string& label,
                                   const std::string& repo_name,
                                   const std::string& source_path,
                                   int64_t project_id,
                                   int64_t* migration_id_out = nullptr);
  void record_repo_migration_complete(int64_t migration_id,
                                      const std::string& label,
                                      bool success,
                                      const std::string& error_message = "");

private:
  config::store* cfg_;
  nexus::Store* store_;
  blackbox::logger* log_;

  // Load from config.toml
  std::optional<ServerConfig> load_from_config(const std::string& label);

  // Load from database
  std::optional<ServerEntry> load_from_database(const std::string& label);

  // Compare config.toml vs database for conflicts
  bool has_config_conflict(const ServerConfig& from_config,
                          const ServerEntry& from_db);

  // Compute hash for conflict detection
  std::string compute_config_hash(const ServerConfig& cfg);
};

} // namespace nazg::git

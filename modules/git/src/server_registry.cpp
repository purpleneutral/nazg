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

#include "git/server_registry.hpp"
#include "config/config.hpp"
#include "nexus/store.hpp"
#include "blackbox/logger.hpp"
#include "prompt/prompt.hpp"
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <ctime>

namespace nazg::git {

ServerRegistry::ServerRegistry(config::store* cfg,
                               nexus::Store* store,
                               blackbox::logger* log)
    : cfg_(cfg), store_(store), log_(log) {}

std::optional<ServerConfig> ServerRegistry::load_from_config(const std::string& label) {
  if (!cfg_) {
    if (log_) {
      log_->warn("Git/registry", "Configuration store not available");
    }
    return std::nullopt;
  }

  // Config format: [git.servers.LABEL]
  std::string section = "git.servers." + label;

  std::string type = cfg_->get_string(section, "type", "");
  if (type.empty()) {
    return std::nullopt;  // Server not in config
  }

  ServerConfig cfg;
  cfg.type = type;
  cfg.host = cfg_->get_string(section, "host", "");
  cfg.ssh_user = cfg_->get_string(section, "ssh_user", "git");
  cfg.ssh_port = cfg_->get_int(section, "ssh_port", 22);
  int default_port = (type == "gitea") ? 3000 : 80;
  cfg.port = cfg_->get_int(section, "port", default_port);
  cfg.repo_base_path = cfg_->get_string(section, "repo_base_path", "/srv/git");
  cfg.config_path = cfg_->get_string(section, "config_path", "");
  cfg.web_url = cfg_->get_string(section, "web_url", "");
  cfg.admin_token = cfg_->get_string(section, "admin_token", "");

  // Set defaults based on type
  if (cfg.config_path.empty()) {
    if (type == "cgit") {
      cfg.config_path = "/etc/cgitrc";
    } else if (type == "gitea") {
      cfg.config_path = "/etc/gitea/app.ini";
    }
  }

  if (cfg.web_url.empty()) {
    if (type == "cgit") {
      cfg.web_url = "http://" + cfg.host + "/cgit";
    } else if (type == "gitea") {
      cfg.web_url = "http://" + cfg.host + ":3000";
    }
  }

  if (cfg.host.empty()) {
    return std::nullopt;  // Host is required
  }

  return cfg;
}

std::optional<ServerEntry> ServerRegistry::load_from_database(const std::string& label) {
  if (!store_) {
    return std::nullopt;
  }

  auto record = store_->get_git_server(label);
  if (!record) {
    return std::nullopt;
  }

  ServerEntry entry;
  entry.id = record->id;
  entry.label = record->label.empty() ? label : record->label;
  entry.config.type = record->type;
  entry.config.host = record->host;
  entry.config.port = record->port;
  entry.config.ssh_port = record->ssh_port;
  entry.config.ssh_user = record->ssh_user.empty() ? "git" : record->ssh_user;
  entry.config.repo_base_path = record->repo_base_path.empty()
                                    ? "/srv/git"
                                    : record->repo_base_path;
  entry.config.config_path = record->config_path;
  entry.config.web_url = record->web_url;
  entry.config.admin_token = record->admin_token;
  entry.admin_token = record->admin_token;
  entry.status = record->status.empty() ? "not_installed" : record->status;
  entry.installed_at = record->installed_at;
  entry.last_check = record->last_check;
  entry.config_hash = record->config_hash;
  entry.config_modified = record->config_modified;
  entry.config_source = "database";

  return entry;
}

std::string ServerRegistry::compute_config_hash(const ServerConfig& cfg) {
  // Simple hash of config values for change detection
  std::ostringstream ss;
  ss << cfg.type << "|"
     << cfg.host << "|"
     << cfg.ssh_user << "|"
     << cfg.ssh_port << "|"
     << cfg.port << "|"
     << cfg.repo_base_path << "|"
     << cfg.config_path << "|"
     << cfg.web_url;

  std::string data = ss.str();

  // Compute SHA256 hash
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256((unsigned char*)data.c_str(), data.length(), hash);

  // Convert to hex string
  std::ostringstream hex;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
    hex << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
  }

  return hex.str();
}

bool ServerRegistry::has_config_conflict(const ServerConfig& from_config,
                                        const ServerEntry& from_db) {
  std::string config_hash = compute_config_hash(from_config);
  return !from_db.config_hash.empty() && config_hash != from_db.config_hash;
}

std::optional<ServerEntry> ServerRegistry::get_server(const std::string& label) {
  ServerEntry entry;
  entry.label = label;

  // Try config.toml first
  auto config_opt = load_from_config(label);

  // Try database
  auto db_opt = load_from_database(label);

  if (!config_opt && !db_opt) {
    // Server not found anywhere
    if (log_) {
      log_->debug("Git/registry", "Server not found: " + label);
    }
    return std::nullopt;
  }

  if (config_opt && !db_opt) {
    // Only in config - first time seeing this server
    entry.config = *config_opt;
    entry.config_source = "toml";
    entry.status = "not_installed";
    entry.config_hash = compute_config_hash(entry.config);
    entry.config_modified = std::time(nullptr);
    entry.has_config_changes = false;
    entry.admin_token = entry.config.admin_token;

    if (log_) {
      log_->debug("Git/registry", "Loaded " + label + " from config.toml");
    }

  } else if (!config_opt && db_opt) {
    // Only in database - config removed?
    entry = *db_opt;
    entry.config_source = "database";
    entry.status = entry.status.empty() ? "not_installed" : entry.status;
    entry.has_config_changes = false;

    if (log_) {
      log_->warn("Git/registry", label + " in database but not in config.toml");
    }

  } else {
    // Both exist - check for conflicts
    entry = *db_opt;  // Start with database entry (has runtime state)
    std::string db_token = entry.admin_token;
    entry.config = *config_opt;  // Update config from toml
    if (entry.config.admin_token.empty()) {
      entry.config.admin_token = db_token;
    }
    entry.admin_token = db_token.empty() ? entry.config.admin_token : db_token;

    std::string new_hash = compute_config_hash(*config_opt);
    entry.has_config_changes = entry.config_hash != new_hash;
    entry.config_source = entry.has_config_changes ? "both (modified)" : "both";

    if (entry.has_config_changes && log_) {
      log_->info("Git/registry", label + " config has changed since last sync");
    }
  }

  return entry;
}

std::vector<ServerEntry> ServerRegistry::list_servers() {
  std::vector<ServerEntry> servers;

  if (store_) {
    auto db_servers = store_->list_git_servers();
    servers.reserve(db_servers.size());
    for (const auto &record : db_servers) {
      auto entry_opt = get_server(record.label);
      if (entry_opt) {
        servers.push_back(*entry_opt);
      }
    }
  }

  if (log_) {
    log_->debug("Git/registry",
                "Listing " + std::to_string(servers.size()) + " server(s)");
  }

  return servers;
}

bool ServerRegistry::add_server(const std::string& label, const ServerConfig& cfg) {
  if (!store_) {
    if (log_) {
      log_->error("Git/registry", "Cannot add server without database connection");
    }
    return false;
  }

  if (cfg.type.empty() || cfg.host.empty()) {
    if (log_) {
      log_->error("Git/registry", "Server config incomplete for label " + label);
    }
    return false;
  }

  int64_t now = std::time(nullptr);
  nexus::GitServer record;
  record.label = label;
  record.type = cfg.type;
  record.host = cfg.host;
  record.port = cfg.port;
  record.ssh_port = cfg.ssh_port;
  record.ssh_user = cfg.ssh_user;
  record.repo_base_path = cfg.repo_base_path;
  record.config_path = cfg.config_path;
  record.web_url = cfg.web_url;
  record.admin_token = cfg.admin_token;
  record.config_hash = compute_config_hash(cfg);
  record.config_modified = now;
  record.updated_at = now;
  record.created_at = now;
  record.status = "not_installed";

  if (auto existing = store_->get_git_server(label)) {
    record.id = existing->id;
    record.status = existing->status.empty() ? record.status : existing->status;
    record.installed_at = existing->installed_at;
    record.last_check = existing->last_check;
    record.created_at = existing->created_at ? existing->created_at : now;
    if (record.admin_token.empty()) {
      record.admin_token = existing->admin_token;
    }
  }

  bool ok = store_->upsert_git_server(record);

  if (!ok) {
    if (log_) {
      log_->error("Git/registry", "Failed to add server " + label);
    }
    return false;
  }

  if (log_) {
    log_->info("Git/registry", "Added server: " + label);
  }

  return true;
}

bool ServerRegistry::update_status(const std::string& label, const std::string& status) {
  if (!store_) {
    if (log_) {
      log_->error("Git/registry", "Cannot update status without database connection");
    }
    return false;
  }

  bool ok = store_->update_git_server_status(label, status);
  if (ok) {
    if (log_) {
      log_->debug("Git/registry", label + " status: " + status);
    }
  } else if (log_) {
    log_->error("Git/registry", "Failed to update status for " + label);
  }
  return ok;
}

bool ServerRegistry::mark_installed(const std::string& label) {
  if (!store_) {
    if (log_) {
      log_->error("Git/registry", "Cannot mark installed without database connection");
    }
    return false;
  }

  int64_t now = std::time(nullptr);
  bool ok = store_->mark_git_server_installed(label, now);
  if (ok) {
    if (log_) {
      log_->info("Git/registry", label + " marked as installed");
    }
  } else if (log_) {
    log_->error("Git/registry", "Failed to mark " + label + " as installed");
  }
  return ok;
}

bool ServerRegistry::update_last_check(const std::string& label, int64_t timestamp) {
  if (!store_) {
    if (log_) {
      log_->error("Git/registry", "Cannot update last_check without database connection");
    }
    return false;
  }

  bool ok = store_->update_git_server_last_check(label, timestamp);
  if (!ok && log_) {
    log_->error("Git/registry", "Failed to update last_check for " + label);
  }
  return ok;
}

bool ServerRegistry::update_admin_token(const std::string& label,
                                        const std::string& token) {
  if (!store_) {
    if (log_) {
      log_->error("Git/registry", "Cannot update admin token without database connection");
    }
    return false;
  }

  bool ok = store_->update_git_server_admin_token(label, token);
  if (!ok && log_) {
    log_->error("Git/registry", "Failed to update admin token for " + label);
  }
  return ok;
}

bool ServerRegistry::sync_config_to_database(const std::string& label, prompt::Prompt* prompt) {
  auto entry_opt = get_server(label);
  if (!entry_opt || !entry_opt->has_config_changes) {
    return true;  // Nothing to sync
  }

  if (prompt) {
    prompt->title("Configuration Changed")
          .question("Sync updated config to database?");

    prompt->fact("Server", label)
          .fact("Host", entry_opt->config.host)
          .fact("Type", entry_opt->config.type);

    prompt->action("Update database with new configuration from config.toml");

    if (!prompt->confirm(true)) {
      if (log_) {
        log_->info("Git/registry", "User declined config sync for " + label);
      }
      return false;
    }
  }

  // Update database with new config
  if (!store_) {
    if (log_) {
      log_->error("Git/registry", "Cannot sync config without database connection");
    }
    return false;
  }

  int64_t now = std::time(nullptr);
  nexus::GitServer record;
  if (auto existing = store_->get_git_server(label)) {
    record = *existing;
  } else {
    record.label = label;
    record.status = "not_installed";
    record.created_at = now;
  }

  record.type = entry_opt->config.type;
  record.host = entry_opt->config.host;
  record.port = entry_opt->config.port;
  record.ssh_port = entry_opt->config.ssh_port;
  record.ssh_user = entry_opt->config.ssh_user;
  record.repo_base_path = entry_opt->config.repo_base_path;
  record.config_path = entry_opt->config.config_path;
  record.web_url = entry_opt->config.web_url;
  record.config_hash = compute_config_hash(entry_opt->config);
  record.config_modified = now;
  record.updated_at = now;

  bool ok = store_->upsert_git_server(record);

  if (ok) {
    if (log_) {
      log_->info("Git/registry", "Synced " + label + " config to database");
    }
  } else if (log_) {
    log_->error("Git/registry", "Failed to sync config for " + label);
  }

  return ok;
}

bool ServerRegistry::record_repo_migration_start(const std::string& label,
                                                 const std::string& repo_name,
                                                 const std::string& source_path,
                                                 int64_t project_id,
                                                 int64_t* migration_id_out) {
  if (!store_) {
    if (log_) {
      log_->error("Git/registry", "Cannot record migration without database connection");
    }
    return false;
  }

  auto server = store_->get_git_server(label);
  if (!server) {
    if (log_) {
      log_->warn("Git/registry", "Cannot record migration; server not found: " + label);
    }
    return false;
  }

  nexus::RepoMigrationRecord rec;
  rec.server_id = server->id;
  rec.project_id = project_id;
  rec.repo_name = repo_name;
  rec.source_path = source_path;
  rec.started_at = std::time(nullptr);
  rec.status = "running";

  int64_t id = store_->add_repo_migration(rec);
  if (id == 0) {
    return false;
  }

  if (migration_id_out) {
    *migration_id_out = id;
  }
  return true;
}

void ServerRegistry::record_repo_migration_complete(int64_t migration_id,
                                                    const std::string& label,
                                                    bool success,
                                                    const std::string& error_message) {
  if (!store_) {
    return;
  }

  if (migration_id == 0) {
    return;
  }

  if (success) {
    store_->mark_repo_migration_completed(migration_id, std::time(nullptr));
  } else {
    store_->update_repo_migration_status(migration_id, "failed", error_message);
  }

  if (log_) {
    if (success) {
      log_->info("Git/registry", "Migration " + std::to_string(migration_id) +
                                    " completed for " + label);
    } else {
      log_->error("Git/registry", "Migration " + std::to_string(migration_id) +
                                     " failed: " + error_message);
    }
  }
}

bool ServerRegistry::remove_server(const std::string& label) {
  if (!store_) {
    if (log_) {
      log_->error("Git/registry", "Cannot remove server without database connection");
    }
    return false;
  }

  bool ok = store_->remove_git_server(label);

  if (ok) {
    if (log_) {
      log_->info("Git/registry", "Removed server: " + label);
    }
  } else if (log_) {
    log_->error("Git/registry", "Failed to remove server: " + label);
  }

  return ok;
}

} // namespace nazg::git

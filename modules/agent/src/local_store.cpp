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

#include "agent/local_store.hpp"
#include "blackbox/logger.hpp"
#include "nexus/sqlite_driver.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace nazg::agent {

std::unique_ptr<LocalStore> LocalStore::create(const std::string &db_path,
                                              ::nazg::blackbox::logger *log) {
  auto conn = std::make_unique<::nazg::nexus::SqliteDriver>(db_path, log);
  if (!conn->is_connected()) {
    if (log) {
      log->error("local_store", "Failed to open database: " + db_path);
    }
    return nullptr;
  }

  return std::make_unique<LocalStore>(std::move(conn), log);
}

LocalStore::LocalStore(std::unique_ptr<::nazg::nexus::Connection> conn,
                     ::nazg::blackbox::logger *log)
    : conn_(std::move(conn)), log_(log) {}

LocalStore::~LocalStore() = default;

bool LocalStore::initialize() {
  if (!run_migrations()) {
    if (log_) {
      log_->error("local_store", "Failed to run migrations");
    }
    return false;
  }

  if (log_) {
    log_->info("local_store", "Agent local store initialized");
  }

  return true;
}

bool LocalStore::run_migrations() {
  // Get current schema version
  std::string query = "SELECT COALESCE(MAX(version), 0) FROM schema_version";
  int64_t current_version = 0;

  try {
    auto result = conn_->query(query);
    if (!result.rows.empty()) {
      current_version = std::stoll(result.rows[0].values[0].value_or("0"));
    }
  } catch (...) {
    // Schema version table doesn't exist yet, version is 0
    current_version = 0;
  }

  // Find and run migration files
  std::string module_dir = __FILE__;
  // Get the directory containing this source file, then navigate to migrations
  size_t last_slash = module_dir.find_last_of("/\\");
  if (last_slash != std::string::npos) {
    module_dir = module_dir.substr(0, last_slash);
    // Go up to module root, then to migrations
    module_dir = module_dir.substr(0, module_dir.find_last_of("/\\"));
    module_dir += "/migrations";
  }

  // For now, just embed the migration inline
  // In production, we'd load from files like the nexus module does
  if (current_version < 1) {
    std::string migration = R"(
      PRAGMA foreign_keys = ON;
      BEGIN TRANSACTION;

      CREATE TABLE IF NOT EXISTS schema_version (
          version INTEGER PRIMARY KEY,
          applied_at INTEGER NOT NULL
      );

      CREATE TABLE IF NOT EXISTS agent_config (
          key TEXT PRIMARY KEY,
          value TEXT NOT NULL,
          updated_at INTEGER NOT NULL
      );

      INSERT OR IGNORE INTO agent_config (key, value, updated_at)
      VALUES ('server_label', 'unknown', strftime('%s', 'now'));

      INSERT OR IGNORE INTO agent_config (key, value, updated_at)
      VALUES ('last_scan', '0', strftime('%s', 'now'));

      INSERT OR IGNORE INTO agent_config (key, value, updated_at)
      VALUES ('control_center_url', '', strftime('%s', 'now'));

      CREATE TABLE IF NOT EXISTS cached_containers (
          container_id TEXT PRIMARY KEY,
          name TEXT NOT NULL,
          image TEXT NOT NULL,
          state TEXT NOT NULL,
          status TEXT,
          created INTEGER,
          service_name TEXT,
          health_status TEXT,
          restart_policy TEXT,
          labels_json TEXT,
          last_seen INTEGER NOT NULL
      );

      CREATE INDEX IF NOT EXISTS idx_cached_containers_state ON cached_containers(state);
      CREATE INDEX IF NOT EXISTS idx_cached_containers_last_seen ON cached_containers(last_seen);

      CREATE TABLE IF NOT EXISTS cached_compose_files (
          path TEXT PRIMARY KEY,
          project_name TEXT,
          services_json TEXT NOT NULL,
          file_hash TEXT,
          last_seen INTEGER NOT NULL
      );

      CREATE TABLE IF NOT EXISTS cached_images (
          image_id TEXT PRIMARY KEY,
          repository TEXT,
          tag TEXT,
          size_bytes INTEGER,
          created INTEGER,
          last_seen INTEGER NOT NULL
      );

      CREATE TABLE IF NOT EXISTS cached_networks (
          network_id TEXT PRIMARY KEY,
          name TEXT NOT NULL,
          driver TEXT,
          scope TEXT,
          last_seen INTEGER NOT NULL
      );

      CREATE TABLE IF NOT EXISTS cached_volumes (
          volume_name TEXT PRIMARY KEY,
          driver TEXT,
          mountpoint TEXT,
          last_seen INTEGER NOT NULL
      );

      CREATE TABLE IF NOT EXISTS scan_history (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          scan_timestamp INTEGER NOT NULL,
          containers_count INTEGER DEFAULT 0,
          images_count INTEGER DEFAULT 0,
          networks_count INTEGER DEFAULT 0,
          volumes_count INTEGER DEFAULT 0,
          compose_files_count INTEGER DEFAULT 0,
          sent_to_control BOOLEAN DEFAULT 0,
          sent_at INTEGER
      );

      CREATE INDEX IF NOT EXISTS idx_scan_history_timestamp ON scan_history(scan_timestamp DESC);

      INSERT OR IGNORE INTO schema_version (version, applied_at)
      VALUES (1, strftime('%s', 'now'));

      COMMIT;
    )";

    if (!conn_->execute_script(migration)) {
      if (log_) {
        log_->error("local_store", "Failed to run migration 001");
      }
      return false;
    }

    if (log_) {
      log_->info("local_store", "Ran migration 001");
    }
  }

  return true;
}

void LocalStore::set_config(const std::string &key, const std::string &value) {
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  std::string query = "INSERT OR REPLACE INTO agent_config (key, value, updated_at) "
                     "VALUES (?, ?, ?)";
  conn_->execute(query, {key, value, std::to_string(timestamp)});
}

std::string LocalStore::get_config(const std::string &key, const std::string &default_val) {
  std::string query = "SELECT value FROM agent_config WHERE key = ?";
  auto result = conn_->query(query, {key});
  if (!result.rows.empty()) {
    auto val = result.rows[0].get("value");
    if (val.has_value()) {
      return val.value();
    }
  }
  return default_val;
}

void LocalStore::set_server_label(const std::string &label) {
  set_config("server_label", label);
}

std::string LocalStore::get_server_label() {
  return get_config("server_label", "unknown");
}

void LocalStore::set_control_center_url(const std::string &url) {
  set_config("control_center_url", url);
}

std::string LocalStore::get_control_center_url() {
  return get_config("control_center_url", "");
}

void LocalStore::update_last_scan_time(int64_t timestamp) {
  set_config("last_scan", std::to_string(timestamp));
}

int64_t LocalStore::get_last_scan_time() {
  std::string val = get_config("last_scan", "0");
  return std::stoll(val);
}

void LocalStore::store_containers(const std::vector<ContainerInfo> &containers, int64_t scan_time) {
  std::string query = "INSERT OR REPLACE INTO cached_containers "
                     "(container_id, name, image, state, status, created, "
                     "service_name, health_status, restart_policy, labels_json, last_seen) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

  for (const auto &c : containers) {
    conn_->execute(query, {
      c.id, c.name, c.image, c.state, c.status,
      std::to_string(c.created), c.service_name, c.health_status,
      c.restart_policy, c.labels_json, std::to_string(scan_time)
    });
  }
}

void LocalStore::store_compose_files(const std::vector<ComposeFileInfo> &files, int64_t scan_time) {
  std::string query = "INSERT OR REPLACE INTO cached_compose_files "
                     "(path, project_name, services_json, file_hash, last_seen) "
                     "VALUES (?, ?, ?, ?, ?)";

  for (const auto &f : files) {
    conn_->execute(query, {
      f.path, f.project_name, f.services_json, f.file_hash,
      std::to_string(scan_time)
    });
  }
}

void LocalStore::store_images(const std::vector<DockerImageInfo> &images, int64_t scan_time) {
  std::string query = "INSERT OR REPLACE INTO cached_images "
                     "(image_id, repository, tag, size_bytes, created, last_seen) "
                     "VALUES (?, ?, ?, ?, ?, ?)";

  for (const auto &i : images) {
    conn_->execute(query, {
      i.id, i.repository, i.tag,
      std::to_string(i.size_bytes), std::to_string(i.created),
      std::to_string(scan_time)
    });
  }
}

void LocalStore::store_networks(const std::vector<DockerNetworkInfo> &networks, int64_t scan_time) {
  std::string query = "INSERT OR REPLACE INTO cached_networks "
                     "(network_id, name, driver, scope, last_seen) "
                     "VALUES (?, ?, ?, ?, ?)";

  for (const auto &n : networks) {
    conn_->execute(query, {
      n.id, n.name, n.driver, n.scope,
      std::to_string(scan_time)
    });
  }
}

void LocalStore::store_volumes(const std::vector<DockerVolumeInfo> &volumes, int64_t scan_time) {
  std::string query = "INSERT OR REPLACE INTO cached_volumes "
                     "(volume_name, driver, mountpoint, last_seen) "
                     "VALUES (?, ?, ?, ?)";

  for (const auto &v : volumes) {
    conn_->execute(query, {
      v.name, v.driver, v.mountpoint,
      std::to_string(scan_time)
    });
  }
}

void LocalStore::record_scan(int64_t timestamp, int containers, int images,
                            int networks, int volumes, int compose_files) {
  std::string query = "INSERT INTO scan_history "
                     "(scan_timestamp, containers_count, images_count, "
                     "networks_count, volumes_count, compose_files_count) "
                     "VALUES (?, ?, ?, ?, ?, ?)";

  conn_->execute(query, {
    std::to_string(timestamp),
    std::to_string(containers),
    std::to_string(images),
    std::to_string(networks),
    std::to_string(volumes),
    std::to_string(compose_files)
  });

  update_last_scan_time(timestamp);
}

void LocalStore::mark_scan_sent(int64_t scan_id) {
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch()).count();

  std::string query = "UPDATE scan_history SET sent_to_control = 1, sent_at = ? WHERE id = ?";
  conn_->execute(query, {
    std::to_string(timestamp),
    std::to_string(scan_id)
  });
}

void LocalStore::cleanup_old_data(int64_t older_than_timestamp) {
  std::vector<std::string> tables = {
    "cached_containers",
    "cached_compose_files",
    "cached_images",
    "cached_networks",
    "cached_volumes"
  };

  for (const auto &table : tables) {
    std::string query = "DELETE FROM " + table + " WHERE last_seen < ?";
    conn_->execute(query, {std::to_string(older_than_timestamp)});
  }
}

std::vector<ContainerInfo> LocalStore::get_cached_containers() {
  std::vector<ContainerInfo> containers;

  std::string query = "SELECT container_id, name, image, state, status, created, "
                     "service_name, health_status, restart_policy, labels_json "
                     "FROM cached_containers ORDER BY name";

  auto result = conn_->query(query);
  for (const auto &row : result.rows) {
    ContainerInfo c;
    c.id = row.get("container_id").value_or("");
    c.name = row.get("name").value_or("");
    c.image = row.get("image").value_or("");
    c.state = row.get("state").value_or("");
    c.status = row.get("status").value_or("");
    c.created = row.get_int("created").value_or(0);
    c.service_name = row.get("service_name").value_or("");
    c.health_status = row.get("health_status").value_or("");
    c.restart_policy = row.get("restart_policy").value_or("");
    c.labels_json = row.get("labels_json").value_or("{}");
    containers.push_back(c);
  }

  return containers;
}

std::vector<ComposeFileInfo> LocalStore::get_cached_compose_files() {
  std::vector<ComposeFileInfo> files;

  std::string query = "SELECT path, project_name, services_json, file_hash "
                     "FROM cached_compose_files ORDER BY path";

  auto result = conn_->query(query);
  for (const auto &row : result.rows) {
    ComposeFileInfo f;
    f.path = row.get("path").value_or("");
    f.project_name = row.get("project_name").value_or("");
    f.services_json = row.get("services_json").value_or("[]");
    f.file_hash = row.get("file_hash").value_or("");
    files.push_back(f);
  }

  return files;
}

std::vector<DockerImageInfo> LocalStore::get_cached_images() {
  std::vector<DockerImageInfo> images;

  std::string query = "SELECT image_id, repository, tag, size_bytes, created "
                     "FROM cached_images ORDER BY repository, tag";

  auto result = conn_->query(query);
  for (const auto &row : result.rows) {
    DockerImageInfo i;
    i.id = row.get("image_id").value_or("");
    i.repository = row.get("repository").value_or("");
    i.tag = row.get("tag").value_or("");
    i.size_bytes = row.get_int("size_bytes").value_or(0);
    i.created = row.get_int("created").value_or(0);
    images.push_back(i);
  }

  return images;
}

std::vector<DockerNetworkInfo> LocalStore::get_cached_networks() {
  std::vector<DockerNetworkInfo> networks;

  std::string query = "SELECT network_id, name, driver, scope "
                     "FROM cached_networks ORDER BY name";

  auto result = conn_->query(query);
  for (const auto &row : result.rows) {
    DockerNetworkInfo n;
    n.id = row.get("network_id").value_or("");
    n.name = row.get("name").value_or("");
    n.driver = row.get("driver").value_or("");
    n.scope = row.get("scope").value_or("");
    networks.push_back(n);
  }

  return networks;
}

std::vector<DockerVolumeInfo> LocalStore::get_cached_volumes() {
  std::vector<DockerVolumeInfo> volumes;

  std::string query = "SELECT volume_name, driver, mountpoint "
                     "FROM cached_volumes ORDER BY volume_name";

  auto result = conn_->query(query);
  for (const auto &row : result.rows) {
    DockerVolumeInfo v;
    v.name = row.get("volume_name").value_or("");
    v.driver = row.get("driver").value_or("");
    v.mountpoint = row.get("mountpoint").value_or("");
    volumes.push_back(v);
  }

  return volumes;
}

} // namespace nazg::agent

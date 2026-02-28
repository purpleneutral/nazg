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

#include "nexus/store.hpp"
#include "nexus/crypto.hpp"
#include "nexus/migrator.hpp"
#include "nexus/sqlite_driver.hpp"
#include "blackbox/logger.hpp"
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace nazg::nexus {

std::unique_ptr<Store> Store::create(const std::string &db_path,
                                     nazg::blackbox::logger *log) {
  if (log)
    log->info("Nexus", "Creating store for " + db_path);
  auto conn = std::make_unique<SqliteDriver>(db_path, log);
  if (!conn->is_connected()) {
    if (log)
      log->error("Nexus", "Failed to create database connection");
    return nullptr;
  }
  return std::make_unique<Store>(std::move(conn), log);
}

Store::Store(std::unique_ptr<Connection> conn, nazg::blackbox::logger *log)
    : conn_(std::move(conn)), log_(log) {
  log_info("Store constructed");
}

Store::~Store() = default;

bool Store::initialize() {
  if (!conn_ || !conn_->is_connected()) {
    log_error("Cannot initialize: no connection");
    last_init_error_ = "Database connection not established";
    return false;
  }

  // Enable WAL mode if SQLite
  if (auto *sqlite = dynamic_cast<SqliteDriver *>(conn_.get())) {
    sqlite->enable_wal_mode();
  }

  // Run migrations
  Migrator mig(conn_.get(), log_);
  if (mig.needs_migration()) {
    log_info("Database needs migration");
    if (!mig.migrate()) {
      log_error("Migration failed");
      last_init_error_ = "Migration failed: " + conn_->last_error();
      return false;
    }
  }

  log_info("Store initialized (version " +
           std::to_string(mig.current_version()) + ")");

  // Load or create encryption salt for token encryption
  auto salt_result = conn_->query(
      "SELECT value FROM nazg_metadata WHERE key = 'encryption_salt' LIMIT 1;");
  if (salt_result.ok && !salt_result.rows.empty()) {
    encryption_salt_ = salt_result.rows[0].get("value").value_or("");
  }
  if (encryption_salt_.empty()) {
    // Generate a random 16-byte salt and store it
    std::random_device rd;
    std::ostringstream oss;
    for (int i = 0; i < 16; ++i) {
      oss << std::hex << std::setfill('0') << std::setw(2)
          << (rd() & 0xFF);
    }
    encryption_salt_ = oss.str();
    conn_->execute(
        "INSERT OR IGNORE INTO nazg_metadata (key, value) "
        "VALUES ('encryption_salt', ?);",
        {encryption_salt_});
  }

  last_init_error_.clear();
  return true;
}

bool Store::reset_database() {
  if (!conn_ || !conn_->is_connected()) {
    log_error("Cannot reset database: no connection");
    return false;
  }

  log_warn("Resetting nexus database (dropping all tables)");

  // Checkpoint WAL to reduce risk of leftover journal artifacts
  conn_->execute("PRAGMA wal_checkpoint(FULL);");

  const char *reset_script =
      "PRAGMA writable_schema = 1;\n"
      "DELETE FROM sqlite_master WHERE type IN ('table','index','trigger');\n"
      "PRAGMA user_version = 0;\n"
      "PRAGMA writable_schema = 0;\n"
      "VACUUM;\n";

  if (!conn_->execute_script(reset_script)) {
    log_error("Failed to reset database: " + conn_->last_error());
    return false;
  }

  // Re-run migrations to recreate schema
  if (!initialize()) {
    log_error("Database re-initialization failed after reset: " + last_init_error_);
    return false;
  }

  log_info("Database reset complete");
  return true;
}

int64_t Store::now_timestamp() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// ===== Projects =====

int64_t Store::ensure_project(const std::string &root_path) {
  log_debug("ensure_project(" + root_path + ") called");
  // Check if exists
  auto existing = get_project_by_path(root_path);
  if (existing) {
    // Update timestamp
    conn_->execute(
        "UPDATE projects SET updated_at = ? WHERE id = ?;",
        {std::to_string(now_timestamp()), std::to_string(existing->id)});
    log_debug("Project " + std::to_string(existing->id) +
              " touched at " + root_path);
    return existing->id;
  }

  // Insert new
  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "INSERT INTO projects (root_path, created_at, updated_at) VALUES (?, ?, "
      "?);",
      {root_path, std::to_string(now), std::to_string(now)});

  if (!ok) {
    log_error("Failed to insert project: " + conn_->last_error());
    return -1;
  }

  int64_t id = conn_->last_insert_id();
  log_info("Created project " + std::to_string(id) + ": " + root_path);
  return id;
}

std::optional<Project> Store::get_project(int64_t id) {
  auto result = conn_->query(
      "SELECT id, root_path, name, language, detected_tools, created_at, "
      "updated_at FROM projects WHERE id = ?;",
      {std::to_string(id)});

  if (!result.ok) {
    log_error("Failed to query project " + std::to_string(id) + ": " + conn_->last_error());
    return std::nullopt;
  }

  if (result.empty())
    return std::nullopt;

  const auto &row = result.rows[0];
  Project p;
  p.id = row.get_int("id").value_or(0);
  p.root_path = row.get("root_path").value_or("");
  p.name = row.get("name").value_or("");
  p.language = row.get("language").value_or("");
  p.detected_tools = row.get("detected_tools").value_or("");
  p.created_at = row.get_int("created_at").value_or(0);
  p.updated_at = row.get_int("updated_at").value_or(0);
  return p;
}

std::optional<Project> Store::get_project_by_path(const std::string &root) {
  auto result = conn_->query(
      "SELECT id, root_path, name, language, detected_tools, created_at, "
      "updated_at FROM projects WHERE root_path = ?;",
      {root});

  if (!result.ok) {
    log_error("Failed to query project by path '" + root + "': " + conn_->last_error());
    return std::nullopt;
  }

  if (result.empty())
    return std::nullopt;

  const auto &row = result.rows[0];
  Project p;
  p.id = row.get_int("id").value_or(0);
  p.root_path = row.get("root_path").value_or("");
  p.name = row.get("name").value_or("");
  p.language = row.get("language").value_or("");
  p.detected_tools = row.get("detected_tools").value_or("");
  p.created_at = row.get_int("created_at").value_or(0);
  p.updated_at = row.get_int("updated_at").value_or(0);
  return p;
}

void Store::update_project(int64_t id, const Project &proj) {
  bool ok = conn_->execute(
      "UPDATE projects SET name = ?, language = ?, detected_tools = ?, "
      "updated_at = ? WHERE id = ?;",
      {proj.name, proj.language, proj.detected_tools,
       std::to_string(now_timestamp()), std::to_string(id)});

  if (!ok) {
    log_error("Failed to update project " + std::to_string(id) + ": " + conn_->last_error());
  } else {
    log_debug("Updated project " + std::to_string(id));
  }
}

std::vector<Project> Store::list_projects() {
  auto result = conn_->query(
      "SELECT id, root_path, name, language, detected_tools, created_at, "
      "updated_at FROM projects ORDER BY updated_at DESC;");

  std::vector<Project> projects;
  if (!result.ok) {
    if (log_)
      log_->error("Nexus", "Failed to list projects: " + conn_->last_error());
    return projects;
  }

  for (const auto &row : result.rows) {
    Project p;
    p.id = row.get_int("id").value_or(0);
    p.root_path = row.get("root_path").value_or("");
    p.name = row.get("name").value_or("");
    p.language = row.get("language").value_or("");
    p.detected_tools = row.get("detected_tools").value_or("");
    p.created_at = row.get_int("created_at").value_or(0);
    p.updated_at = row.get_int("updated_at").value_or(0);
    projects.push_back(p);
  }

  return projects;
}

// ===== Facts =====

void Store::set_fact(int64_t project_id, const std::string &ns,
                     const std::string &key, const std::string &value) {
  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "INSERT INTO facts (project_id, namespace, key, value, created_at, "
      "updated_at) VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(project_id, "
      "namespace, key) DO UPDATE SET value = excluded.value, updated_at = "
      "excluded.updated_at;",
      {std::to_string(project_id), ns, key, value, std::to_string(now),
       std::to_string(now)});

  if (ok) {
    log_debug("Set fact " + ns + "." + key + " for project " +
              std::to_string(project_id));
  } else {
    log_error("Failed to set fact " + ns + "." + key + ": " + conn_->last_error());
  }
}

std::optional<std::string> Store::get_fact(int64_t project_id,
                                           const std::string &ns,
                                           const std::string &key) {
  auto result = conn_->query(
      "SELECT value FROM facts WHERE project_id = ? AND namespace = ? AND key "
      "= ?;",
      {std::to_string(project_id), ns, key});

  if (!result.ok) {
    log_error("Failed to get fact " + ns + "." + key + ": " + conn_->last_error());
    return std::nullopt;
  }

  if (result.empty())
    return std::nullopt;

  return result.rows[0].get("value");
}

std::map<std::string, std::string>
Store::list_facts(int64_t project_id, const std::string &ns) {
  auto result = conn_->query(
      "SELECT key, value FROM facts WHERE project_id = ? AND namespace = ? "
      "ORDER BY key;",
      {std::to_string(project_id), ns});

  std::map<std::string, std::string> facts;
  if (!result.ok) {
    log_error("Failed to list facts for namespace '" + ns + "': " + conn_->last_error());
    return facts;
  }

  for (const auto &row : result.rows) {
    auto k = row.get("key");
    auto v = row.get("value");
    if (k && v)
      facts[*k] = *v;
  }

  return facts;
}

// ===== Events =====

void Store::add_event(int64_t project_id, const std::string &level,
                      const std::string &tag, const std::string &message,
                      const std::string &metadata) {
  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "INSERT INTO events (project_id, level, tag, message, metadata, "
      "created_at) VALUES (?, ?, ?, ?, ?, ?);",
      {std::to_string(project_id), level, tag, message, metadata,
       std::to_string(now)});

  if (!ok) {
    log_warn("Failed to add event: " + conn_->last_error());
  } else {
    log_debug("Recorded event '" + tag + "' for project " +
              std::to_string(project_id));
  }
}

std::vector<Event> Store::recent_events(int64_t project_id, int limit) {
  auto result = conn_->query(
      "SELECT id, project_id, level, tag, message, metadata, created_at FROM "
      "events WHERE project_id = ? ORDER BY created_at DESC LIMIT ?;",
      {std::to_string(project_id), std::to_string(limit)});

  std::vector<Event> events;
  if (!result.ok) {
    if (log_)
      log_->error("Nexus", "Failed to query recent events: " + conn_->last_error());
    return events;
  }

  for (const auto &row : result.rows) {
    Event e;
    e.id = row.get_int("id").value_or(0);
    e.project_id = row.get_int("project_id").value_or(0);
    e.level = row.get("level").value_or("");
    e.tag = row.get("tag").value_or("");
    e.message = row.get("message").value_or("");
    e.metadata = row.get("metadata").value_or("");
    e.created_at = row.get_int("created_at").value_or(0);
    events.push_back(e);
  }

  return events;
}

// ===== Command History =====

void Store::record_command(int64_t project_id, const std::string &cmd,
                            const std::vector<std::string> &args, int exit_code,
                            int64_t duration_ms) {
  // Convert args to JSON array
  std::ostringstream args_json;
  args_json << "[";
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0)
      args_json << ",";
    args_json << "\"" << args[i] << "\""; // Naive JSON escaping
  }
  args_json << "]";

  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "INSERT INTO command_history (project_id, command, args, exit_code, "
      "duration_ms, executed_at) VALUES (?, ?, ?, ?, ?, ?);",
      {std::to_string(project_id), cmd, args_json.str(),
       std::to_string(exit_code), std::to_string(duration_ms),
       std::to_string(now)});

  if (!ok) {
    log_warn("Failed to record command: " + conn_->last_error());
  } else {
    log_debug("Recorded command '" + cmd + "' (exit=" +
              std::to_string(exit_code) + ")");
  }
}

std::vector<CommandRecord> Store::recent_commands(int limit) {
  auto result = conn_->query(
      "SELECT id, project_id, command, args, exit_code, duration_ms, "
      "executed_at FROM command_history ORDER BY executed_at DESC LIMIT ?;",
      {std::to_string(limit)});

  std::vector<CommandRecord> commands;
  if (!result.ok) {
    if (log_)
      log_->error("Nexus", "Failed to query recent commands: " + conn_->last_error());
    return commands;
  }

  for (const auto &row : result.rows) {
    CommandRecord c;
    c.id = row.get_int("id").value_or(0);
    c.project_id = row.get_int("project_id").value_or(0);
    c.command = row.get("command").value_or("");
    c.args = row.get("args").value_or("");
    c.exit_code = row.get_int("exit_code").value_or(0);
    c.duration_ms = row.get_int("duration_ms").value_or(0);
    c.executed_at = row.get_int("executed_at").value_or(0);
    commands.push_back(c);
  }

  return commands;
}

std::vector<CommandRecord>
Store::recent_commands_for_project(int64_t project_id, int limit) {
  auto result = conn_->query(
      "SELECT id, project_id, command, args, exit_code, duration_ms, "
      "executed_at FROM command_history WHERE project_id = ? ORDER BY "
      "executed_at DESC LIMIT ?;",
      {std::to_string(project_id), std::to_string(limit)});

  std::vector<CommandRecord> commands;
  if (!result.ok) {
    if (log_)
      log_->error("Nexus", "Failed to query commands for project " + std::to_string(project_id) + ": " + conn_->last_error());
    return commands;
  }

  for (const auto &row : result.rows) {
    CommandRecord c;
    c.id = row.get_int("id").value_or(0);
    c.project_id = row.get_int("project_id").value_or(0);
    c.command = row.get("command").value_or("");
    c.args = row.get("args").value_or("");
    c.exit_code = row.get_int("exit_code").value_or(0);
    c.duration_ms = row.get_int("duration_ms").value_or(0);
    c.executed_at = row.get_int("executed_at").value_or(0);
    commands.push_back(c);
  }

  return commands;
}

// ===== Snapshots =====

int64_t Store::add_snapshot(int64_t project_id, const std::string &hash,
                            int file_count, int64_t total_bytes) {
  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "INSERT INTO snapshots (project_id, tree_hash, file_count, total_bytes, "
      "created_at) VALUES (?, ?, ?, ?, ?);",
      {std::to_string(project_id), hash, std::to_string(file_count),
       std::to_string(total_bytes), std::to_string(now)});

  if (!ok) {
    if (log_)
      log_->error("Nexus", "Failed to add snapshot: " + conn_->last_error());
    return -1;
  }

  int64_t id = conn_->last_insert_id();
  if (log_)
    log_->debug("Nexus", "Snapshot " + std::to_string(id) + " added for project " + std::to_string(project_id));

  return id;
}

std::optional<Snapshot> Store::latest_snapshot(int64_t project_id) {
  auto result = conn_->query(
      "SELECT id, project_id, tree_hash, file_count, total_bytes, created_at "
      "FROM snapshots WHERE project_id = ? ORDER BY created_at DESC LIMIT 1;",
      {std::to_string(project_id)});

  if (!result.ok) {
    if (log_)
      log_->error("Nexus", "Failed to query latest snapshot: " + conn_->last_error());
    return std::nullopt;
  }

  if (result.empty())
    return std::nullopt;

  const auto &row = result.rows[0];
  Snapshot s;
  s.id = row.get_int("id").value_or(0);
  s.project_id = row.get_int("project_id").value_or(0);
  s.tree_hash = row.get("tree_hash").value_or("");
  s.file_count = row.get_int("file_count").value_or(0);
  s.total_bytes = row.get_int("total_bytes").value_or(0);
  s.created_at = row.get_int("created_at").value_or(0);
  return s;
}

// ===== Maintenance =====

void Store::prune_events(int64_t project_id, int keep_count) {
  bool ok = conn_->execute(
      "DELETE FROM events WHERE project_id = ? AND id NOT IN (SELECT id FROM "
      "events WHERE project_id = ? ORDER BY created_at DESC LIMIT ?);",
      {std::to_string(project_id), std::to_string(project_id),
       std::to_string(keep_count)});

  if (!ok && log_) {
    log_->warn("Nexus", "Failed to prune events: " + conn_->last_error());
  } else if (log_) {
    log_->debug("Nexus", "Pruned events for project " + std::to_string(project_id));
  }
}

void Store::prune_commands(int keep_count) {
  bool ok = conn_->execute("DELETE FROM command_history WHERE id NOT IN (SELECT id FROM "
                           "command_history ORDER BY executed_at DESC LIMIT ?);",
                           {std::to_string(keep_count)});

  if (!ok && log_) {
    log_->warn("Nexus", "Failed to prune command history: " + conn_->last_error());
  } else if (log_) {
    log_->debug("Nexus", "Pruned command history");
  }
}

// ===== Bot Hosts =====

int64_t Store::add_bot_host(const std::string &label, const std::string &address,
                             const std::string &ssh_config) {
  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "INSERT INTO bot_hosts (label, address, ssh_config, created_at, updated_at) "
      "VALUES (?, ?, ?, ?, ?);",
      {label, address, ssh_config, std::to_string(now), std::to_string(now)});

  if (!ok) {
    if (log_)
      log_->error("Nexus", "Failed to insert bot host: " + conn_->last_error());
    return -1;
  }

  int64_t id = conn_->last_insert_id();
  if (log_)
    log_->debug("Nexus", "Created bot host " + std::to_string(id) + ": " + label);
  return id;
}

std::optional<int64_t> Store::get_bot_host_id(const std::string &label) {
  auto result = conn_->query("SELECT id FROM bot_hosts WHERE label = ?;", {label});

  if (!result.ok || result.empty())
    return std::nullopt;

  return result.rows[0].get_int("id");
}

void Store::update_bot_host_status(int64_t host_id, const std::string &status) {
  int64_t now = now_timestamp();
  conn_->execute(
      "UPDATE bot_hosts SET last_status = ?, last_run_at = ?, updated_at = ? WHERE id = ?;",
      {status, std::to_string(now), std::to_string(now), std::to_string(host_id)});
}

std::vector<std::map<std::string, std::string>> Store::list_bot_hosts() {
  auto result = conn_->query(
      "SELECT id, label, address, ssh_config, last_status, last_run_at FROM bot_hosts ORDER BY label;", {});

  std::vector<std::map<std::string, std::string>> hosts;
  for (const auto &row : result.rows) {
    std::map<std::string, std::string> host;
    host["id"] = std::to_string(row.get_int("id").value_or(0));
    host["label"] = row.get("label").value_or("");
    host["address"] = row.get("address").value_or("");
    host["ssh_config"] = row.get("ssh_config").value_or("");
    host["last_status"] = row.get("last_status").value_or("");
    host["last_run_at"] = std::to_string(row.get_int("last_run_at").value_or(0));
    hosts.push_back(host);
  }
  return hosts;
}

bool Store::update_bot_host(int64_t host_id, const std::string &label,
                            const std::string &address,
                            const std::string &ssh_config) {
  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "UPDATE bot_hosts SET label = ?, address = ?, ssh_config = ?, updated_at = ? WHERE id = ?;",
      {label, address, ssh_config, std::to_string(now), std::to_string(host_id)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to update bot host: " + conn_->last_error());
  }

  return ok;
}

bool Store::delete_bot_host(int64_t host_id) {
  bool ok = conn_->execute("DELETE FROM bot_hosts WHERE id = ?;",
                           {std::to_string(host_id)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to delete bot host: " + conn_->last_error());
  }

  return ok;
}

// ===== Bot Runs =====

int64_t Store::begin_bot_run(const std::string &bot_name, int64_t host_id) {
  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "INSERT INTO bot_runs (bot_name, host_id, started_at, status) VALUES (?, ?, ?, 'running');",
      {bot_name, std::to_string(host_id), std::to_string(now)});

  if (!ok) {
    if (log_)
      log_->error("Nexus", "Failed to begin bot run: " + conn_->last_error());
    return -1;
  }

  int64_t run_id = conn_->last_insert_id();
  if (log_)
    log_->debug("Nexus", "Started bot run " + std::to_string(run_id) + " for " + bot_name);
  return run_id;
}

void Store::finish_bot_run(int64_t run_id, const std::string &status, int exit_code,
                            int64_t duration_ms) {
  int64_t now = now_timestamp();
  conn_->execute(
      "UPDATE bot_runs SET finished_at = ?, status = ?, exit_code = ?, duration_ms = ? WHERE id = ?;",
      {std::to_string(now), status, std::to_string(exit_code), std::to_string(duration_ms),
       std::to_string(run_id)});

  if (log_)
    log_->debug("Nexus", "Finished bot run " + std::to_string(run_id) + ": " + status);
}

std::vector<std::map<std::string, std::string>>
Store::recent_bot_runs(const std::string &bot_name, int limit) {
  auto result = conn_->query(
      "SELECT r.id, r.bot_name, r.status, r.exit_code, r.started_at, r.finished_at, r.duration_ms, "
      "h.label as host_label FROM bot_runs r JOIN bot_hosts h ON r.host_id = h.id "
      "WHERE r.bot_name = ? ORDER BY r.started_at DESC LIMIT ?;",
      {bot_name, std::to_string(limit)});

  std::vector<std::map<std::string, std::string>> runs;
  for (const auto &row : result.rows) {
    std::map<std::string, std::string> run;
    run["id"] = std::to_string(row.get_int("id").value_or(0));
    run["bot_name"] = row.get("bot_name").value_or("");
    run["status"] = row.get("status").value_or("");
    run["exit_code"] = std::to_string(row.get_int("exit_code").value_or(0));
    run["started_at"] = std::to_string(row.get_int("started_at").value_or(0));
    run["finished_at"] = std::to_string(row.get_int("finished_at").value_or(0));
    run["duration_ms"] = std::to_string(row.get_int("duration_ms").value_or(0));
    run["host_label"] = row.get("host_label").value_or("");
    runs.push_back(run);
  }
  return runs;
}

std::vector<std::map<std::string, std::string>>
Store::recent_bot_runs_for_host(int64_t host_id, int limit) {
  auto result = conn_->query(
      "SELECT id, bot_name, status, exit_code, started_at, finished_at, duration_ms "
      "FROM bot_runs WHERE host_id = ? ORDER BY started_at DESC LIMIT ?;",
      {std::to_string(host_id), std::to_string(limit)});

  std::vector<std::map<std::string, std::string>> runs;
  for (const auto &row : result.rows) {
    std::map<std::string, std::string> run;
    run["id"] = std::to_string(row.get_int("id").value_or(0));
    run["bot_name"] = row.get("bot_name").value_or("");
    run["status"] = row.get("status").value_or("");
    run["exit_code"] = std::to_string(row.get_int("exit_code").value_or(0));
    run["started_at"] = std::to_string(row.get_int("started_at").value_or(0));
    run["finished_at"] = std::to_string(row.get_int("finished_at").value_or(0));
    run["duration_ms"] = std::to_string(row.get_int("duration_ms").value_or(0));
    runs.push_back(run);
  }
  return runs;
}

// ===== Bot Reports =====

void Store::add_bot_report(int64_t run_id, const std::string &json_payload) {
  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "INSERT INTO bot_reports (run_id, payload_json, created_at) VALUES (?, ?, ?);",
      {std::to_string(run_id), json_payload, std::to_string(now)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to insert bot report: " + conn_->last_error());
  } else if (log_) {
    log_->debug("Nexus", "Added bot report for run " + std::to_string(run_id));
  }
}

std::optional<std::string> Store::latest_bot_report(const std::string &bot_name, int64_t host_id) {
  auto result = conn_->query(
      "SELECT rep.payload_json FROM bot_reports rep "
      "JOIN bot_runs run ON rep.run_id = run.id "
      "WHERE run.bot_name = ? AND run.host_id = ? "
      "ORDER BY run.started_at DESC LIMIT 1;",
      {bot_name, std::to_string(host_id)});

  if (!result.ok || result.empty())
    return std::nullopt;

  return result.rows[0].get("payload_json");
}

// ===== Test Runs =====

int64_t Store::add_test_run(int64_t project_id, const std::string &framework,
                            int64_t timestamp, int64_t duration_ms, int exit_code,
                            int total, int passed, int failed, int skipped,
                            int errors, const std::string &triggered_by) {
  bool ok = conn_->execute(
      "INSERT INTO test_runs (project_id, framework, timestamp, duration_ms, exit_code, "
      "total_tests, passed, failed, skipped, errors, triggered_by) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
      {std::to_string(project_id), framework, std::to_string(timestamp),
       std::to_string(duration_ms), std::to_string(exit_code), std::to_string(total),
       std::to_string(passed), std::to_string(failed), std::to_string(skipped),
       std::to_string(errors), triggered_by});

  if (!ok) {
    if (log_)
      log_->error("Nexus", "Failed to add test run: " + conn_->last_error());
    return -1;
  }

  int64_t run_id = conn_->last_insert_id();
  if (log_)
    log_->debug("Nexus", "Added test run " + std::to_string(run_id) +
                         " for project " + std::to_string(project_id));
  return run_id;
}

std::optional<std::map<std::string, std::string>>
Store::get_test_run(int64_t run_id) {
  auto result = conn_->query(
      "SELECT id, project_id, framework, timestamp, duration_ms, exit_code, "
      "total_tests, passed, failed, skipped, errors, triggered_by FROM test_runs "
      "WHERE id = ?;",
      {std::to_string(run_id)});

  if (result.rows.empty())
    return std::nullopt;

  const auto &row = result.rows[0];
  std::map<std::string, std::string> run;
  run["id"] = std::to_string(row.get_int("id").value_or(0));
  run["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
  run["framework"] = row.get("framework").value_or("");
  run["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
  run["duration_ms"] = std::to_string(row.get_int("duration_ms").value_or(0));
  run["exit_code"] = std::to_string(row.get_int("exit_code").value_or(0));
  run["total_tests"] = std::to_string(row.get_int("total_tests").value_or(0));
  run["passed"] = std::to_string(row.get_int("passed").value_or(0));
  run["failed"] = std::to_string(row.get_int("failed").value_or(0));
  run["skipped"] = std::to_string(row.get_int("skipped").value_or(0));
  run["errors"] = std::to_string(row.get_int("errors").value_or(0));
  run["triggered_by"] = row.get("triggered_by").value_or("");
  return run;
}

std::vector<std::map<std::string, std::string>>
Store::get_test_runs(int64_t project_id, int limit) {
  auto result = conn_->query(
      "SELECT id, project_id, framework, timestamp, duration_ms, exit_code, "
      "total_tests, passed, failed, skipped, errors, triggered_by FROM test_runs "
      "WHERE project_id = ? ORDER BY timestamp DESC LIMIT ?;",
      {std::to_string(project_id), std::to_string(limit)});

  std::vector<std::map<std::string, std::string>> runs;
  for (const auto &row : result.rows) {
    std::map<std::string, std::string> run;
    run["id"] = std::to_string(row.get_int("id").value_or(0));
    run["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    run["framework"] = row.get("framework").value_or("");
    run["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
    run["duration_ms"] = std::to_string(row.get_int("duration_ms").value_or(0));
    run["exit_code"] = std::to_string(row.get_int("exit_code").value_or(0));
    run["total_tests"] = std::to_string(row.get_int("total_tests").value_or(0));
    run["passed"] = std::to_string(row.get_int("passed").value_or(0));
    run["failed"] = std::to_string(row.get_int("failed").value_or(0));
    run["skipped"] = std::to_string(row.get_int("skipped").value_or(0));
    run["errors"] = std::to_string(row.get_int("errors").value_or(0));
    run["triggered_by"] = row.get("triggered_by").value_or("");
    runs.push_back(run);
  }
  return runs;
}

std::optional<std::map<std::string, std::string>>
Store::get_latest_test_run(int64_t project_id) {
  auto result = conn_->query(
      "SELECT id, project_id, framework, timestamp, duration_ms, exit_code, "
      "total_tests, passed, failed, skipped, errors, triggered_by FROM test_runs "
      "WHERE project_id = ? ORDER BY timestamp DESC LIMIT 1;",
      {std::to_string(project_id)});

  if (result.rows.empty())
    return std::nullopt;

  const auto &row = result.rows[0];
  std::map<std::string, std::string> run;
  run["id"] = std::to_string(row.get_int("id").value_or(0));
  run["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
  run["framework"] = row.get("framework").value_or("");
  run["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
  run["duration_ms"] = std::to_string(row.get_int("duration_ms").value_or(0));
  run["exit_code"] = std::to_string(row.get_int("exit_code").value_or(0));
  run["total_tests"] = std::to_string(row.get_int("total_tests").value_or(0));
  run["passed"] = std::to_string(row.get_int("passed").value_or(0));
  run["failed"] = std::to_string(row.get_int("failed").value_or(0));
  run["skipped"] = std::to_string(row.get_int("skipped").value_or(0));
  run["errors"] = std::to_string(row.get_int("errors").value_or(0));
  run["triggered_by"] = row.get("triggered_by").value_or("");
  return run;
}

// ===== Test Results =====

bool Store::add_test_result(int64_t run_id, const std::string &suite,
                             const std::string &name, const std::string &status,
                             int64_t duration_ms, const std::string &message,
                             const std::string &file, int line) {
  bool ok = conn_->execute(
      "INSERT INTO test_results (run_id, suite, name, status, duration_ms, message, file, line) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
      {std::to_string(run_id), suite, name, status, std::to_string(duration_ms),
       message, file, std::to_string(line)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to add test result: " + conn_->last_error());
  }

  return ok;
}

std::vector<std::map<std::string, std::string>>
Store::get_test_results(int64_t run_id) {
  auto result = conn_->query(
      "SELECT id, run_id, suite, name, status, duration_ms, message, file, line "
      "FROM test_results WHERE run_id = ? ORDER BY id;",
      {std::to_string(run_id)});

  std::vector<std::map<std::string, std::string>> results;
  for (const auto &row : result.rows) {
    std::map<std::string, std::string> test;
    test["id"] = std::to_string(row.get_int("id").value_or(0));
    test["run_id"] = std::to_string(row.get_int("run_id").value_or(0));
    test["suite"] = row.get("suite").value_or("");
    test["name"] = row.get("name").value_or("");
    test["status"] = row.get("status").value_or("");
    test["duration_ms"] = std::to_string(row.get_int("duration_ms").value_or(0));
    test["message"] = row.get("message").value_or("");
    test["file"] = row.get("file").value_or("");
    test["line"] = std::to_string(row.get_int("line").value_or(0));
    results.push_back(test);
  }
  return results;
}

std::vector<std::map<std::string, std::string>>
Store::get_failed_test_results(int64_t run_id) {
  auto result = conn_->query(
      "SELECT id, run_id, suite, name, status, duration_ms, message, file, line "
      "FROM test_results WHERE run_id = ? AND status = 'failed' ORDER BY id;",
      {std::to_string(run_id)});

  std::vector<std::map<std::string, std::string>> results;
  for (const auto &row : result.rows) {
    std::map<std::string, std::string> test;
    test["id"] = std::to_string(row.get_int("id").value_or(0));
    test["run_id"] = std::to_string(row.get_int("run_id").value_or(0));
    test["suite"] = row.get("suite").value_or("");
    test["name"] = row.get("name").value_or("");
    test["status"] = row.get("status").value_or("");
    test["duration_ms"] = std::to_string(row.get_int("duration_ms").value_or(0));
    test["message"] = row.get("message").value_or("");
    test["file"] = row.get("file").value_or("");
    test["line"] = std::to_string(row.get_int("line").value_or(0));
    results.push_back(test);
  }
  return results;
}

// ===== Test Coverage =====

bool Store::add_test_coverage(int64_t run_id, const std::string &file_path,
                               double line_coverage, double branch_coverage,
                               int lines_covered, int lines_total,
                               int branches_covered, int branches_total) {
  bool ok = conn_->execute(
      "INSERT INTO test_coverage (run_id, file_path, line_coverage, branch_coverage, "
      "lines_covered, lines_total, branches_covered, branches_total) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
      {std::to_string(run_id), file_path, std::to_string(line_coverage),
       std::to_string(branch_coverage), std::to_string(lines_covered),
       std::to_string(lines_total), std::to_string(branches_covered),
       std::to_string(branches_total)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to add test coverage: " + conn_->last_error());
  }

  return ok;
}

std::optional<std::map<std::string, std::string>>
Store::get_test_coverage_summary(int64_t run_id) {
  auto result = conn_->query(
      "SELECT AVG(line_coverage) as avg_line_cov, AVG(branch_coverage) as avg_branch_cov, "
      "SUM(lines_covered) as total_lines_covered, SUM(lines_total) as total_lines, "
      "SUM(branches_covered) as total_branches_covered, SUM(branches_total) as total_branches "
      "FROM test_coverage WHERE run_id = ?;",
      {std::to_string(run_id)});

  if (result.rows.empty())
    return std::nullopt;

  const auto &row = result.rows[0];
  std::map<std::string, std::string> summary;
  summary["avg_line_coverage"] = row.get("avg_line_cov").value_or("0.0");
  summary["avg_branch_coverage"] = row.get("avg_branch_cov").value_or("0.0");
  summary["total_lines_covered"] = std::to_string(row.get_int("total_lines_covered").value_or(0));
  summary["total_lines"] = std::to_string(row.get_int("total_lines").value_or(0));
  summary["total_branches_covered"] = std::to_string(row.get_int("total_branches_covered").value_or(0));
  summary["total_branches"] = std::to_string(row.get_int("total_branches").value_or(0));
  return summary;
}

std::vector<std::map<std::string, std::string>>
Store::get_test_coverage_files(int64_t run_id) {
  auto result = conn_->query(
      "SELECT id, run_id, file_path, line_coverage, branch_coverage, "
      "lines_covered, lines_total, branches_covered, branches_total "
      "FROM test_coverage WHERE run_id = ? ORDER BY file_path;",
      {std::to_string(run_id)});

  std::vector<std::map<std::string, std::string>> files;
  for (const auto &row : result.rows) {
    std::map<std::string, std::string> file;
    file["id"] = std::to_string(row.get_int("id").value_or(0));
    file["run_id"] = std::to_string(row.get_int("run_id").value_or(0));
    file["file_path"] = row.get("file_path").value_or("");
    file["line_coverage"] = row.get("line_coverage").value_or("0.0");
    file["branch_coverage"] = row.get("branch_coverage").value_or("0.0");
    file["lines_covered"] = std::to_string(row.get_int("lines_covered").value_or(0));
    file["lines_total"] = std::to_string(row.get_int("lines_total").value_or(0));
    file["branches_covered"] = std::to_string(row.get_int("branches_covered").value_or(0));
    file["branches_total"] = std::to_string(row.get_int("branches_total").value_or(0));
    files.push_back(file);
  }
  return files;
}

// ===== Git Servers =====

std::optional<GitServer> Store::get_git_server(const std::string &label) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot query git server: no database connection");
    }
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, label, type, host, port, ssh_port, ssh_user, repo_base_path, "
      "config_path, web_url, status, installed_at, last_check, config_hash, "
      "config_modified, created_at, updated_at, admin_token "
      "FROM git_servers WHERE label = ? LIMIT 1;",
      {label});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to get git server '" + label + "': " +
                              conn_->last_error());
    }
    return std::nullopt;
  }

  if (result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  GitServer server;
  server.id = row.get_int("id").value_or(0);
  server.label = row.get("label").value_or("");
  server.type = row.get("type").value_or("");
  server.host = row.get("host").value_or("");
  server.port = static_cast<int>(row.get_int("port").value_or(0));
  server.ssh_port = static_cast<int>(row.get_int("ssh_port").value_or(22));
  server.ssh_user = row.get("ssh_user").value_or("");
  server.repo_base_path = row.get("repo_base_path").value_or("");
  server.config_path = row.get("config_path").value_or("");
  server.web_url = row.get("web_url").value_or("");
  server.status = row.get("status").value_or("");
  server.installed_at = row.get_int("installed_at").value_or(0);
  server.last_check = row.get_int("last_check").value_or(0);
  server.config_hash = row.get("config_hash").value_or("");
  server.config_modified = row.get_int("config_modified").value_or(0);
  server.created_at = row.get_int("created_at").value_or(0);
  server.updated_at = row.get_int("updated_at").value_or(0);
  auto raw_token = row.get("admin_token").value_or("");
  server.admin_token = decrypt_token(raw_token, encryption_salt_).value_or("");
  return server;
}

std::vector<GitServer> Store::list_git_servers() {
  std::vector<GitServer> servers;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot list git servers: no database connection");
    }
    return servers;
  }

  auto result = conn_->query(
      "SELECT id, label, type, host, port, ssh_port, ssh_user, repo_base_path, "
      "config_path, web_url, status, installed_at, last_check, config_hash, "
      "config_modified, created_at, updated_at, admin_token "
      "FROM git_servers ORDER BY label;");

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to list git servers: " + conn_->last_error());
    }
    return servers;
  }

  for (const auto &row : result.rows) {
    GitServer server;
    server.id = row.get_int("id").value_or(0);
    server.label = row.get("label").value_or("");
    server.type = row.get("type").value_or("");
    server.host = row.get("host").value_or("");
    server.port = static_cast<int>(row.get_int("port").value_or(0));
    server.ssh_port = static_cast<int>(row.get_int("ssh_port").value_or(22));
    server.ssh_user = row.get("ssh_user").value_or("");
    server.repo_base_path = row.get("repo_base_path").value_or("");
    server.config_path = row.get("config_path").value_or("");
    server.web_url = row.get("web_url").value_or("");
    server.status = row.get("status").value_or("");
    server.installed_at = row.get_int("installed_at").value_or(0);
    server.last_check = row.get_int("last_check").value_or(0);
    server.config_hash = row.get("config_hash").value_or("");
    server.config_modified = row.get_int("config_modified").value_or(0);
    server.created_at = row.get_int("created_at").value_or(0);
    server.updated_at = row.get_int("updated_at").value_or(0);
    auto raw_token = row.get("admin_token").value_or("");
    server.admin_token = decrypt_token(raw_token, encryption_salt_).value_or("");
    servers.push_back(std::move(server));
  }

  return servers;
}

bool Store::upsert_git_server(const GitServer &server) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot upsert git server: no database connection");
    }
    return false;
  }

  int64_t now = now_timestamp();
  int64_t created = server.created_at != 0 ? server.created_at : now;
  int64_t updated = server.updated_at != 0 ? server.updated_at : now;
  int64_t config_modified =
      server.config_modified != 0 ? server.config_modified : now;

  std::string sql =
      "INSERT INTO git_servers (label, type, host, port, ssh_port, ssh_user, "
      "repo_base_path, config_path, web_url, status, installed_at, last_check, "
      "config_hash, config_modified, created_at, updated_at, admin_token) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(label) DO UPDATE SET "
      "type = excluded.type, "
      "host = excluded.host, "
      "port = excluded.port, "
      "ssh_port = excluded.ssh_port, "
      "ssh_user = excluded.ssh_user, "
      "repo_base_path = excluded.repo_base_path, "
      "config_path = excluded.config_path, "
      "web_url = excluded.web_url, "
      "status = excluded.status, "
      "installed_at = excluded.installed_at, "
      "last_check = excluded.last_check, "
      "config_hash = excluded.config_hash, "
      "config_modified = excluded.config_modified, "
      "updated_at = excluded.updated_at, "
      "admin_token = excluded.admin_token;";

  std::string stored_token = server.admin_token.empty()
      ? std::string()
      : encrypt_token(server.admin_token, encryption_salt_);

  bool ok = conn_->execute(sql,
                           {server.label,
                            server.type,
                            server.host,
                            std::to_string(server.port),
                            std::to_string(server.ssh_port),
                            server.ssh_user,
                            server.repo_base_path,
                            server.config_path,
                            server.web_url,
                            server.status,
                            std::to_string(server.installed_at),
                            std::to_string(server.last_check),
                            server.config_hash,
                            std::to_string(config_modified),
                            std::to_string(created),
                            std::to_string(updated),
                            stored_token});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to upsert git server '" + server.label +
                             "': " + conn_->last_error());
  }

  return ok;
}

bool Store::update_git_server_status(const std::string &label,
                                     const std::string &status) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot update git server status: no database connection");
    }
    return false;
  }

  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "UPDATE git_servers SET status = ?, updated_at = ? WHERE label = ?;",
      {status, std::to_string(now), label});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to update git server status for '" + label +
                             "': " + conn_->last_error());
  }

  return ok;
}

bool Store::mark_git_server_installed(const std::string &label,
                                      int64_t timestamp) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot mark git server installed: no database connection");
    }
    return false;
  }

  int64_t when = timestamp != 0 ? timestamp : now_timestamp();
  bool ok = conn_->execute(
      "UPDATE git_servers SET status = 'installed', installed_at = ?, "
      "updated_at = ? WHERE label = ?;",
      {std::to_string(when), std::to_string(when), label});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to mark git server installed for '" + label +
                             "': " + conn_->last_error());
  }

  return ok;
}

bool Store::update_git_server_last_check(const std::string &label,
                                         int64_t timestamp) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot update git server last_check: no database connection");
    }
    return false;
  }

  int64_t when = timestamp != 0 ? timestamp : now_timestamp();
  bool ok = conn_->execute(
      "UPDATE git_servers SET last_check = ?, updated_at = ? WHERE label = ?;",
      {std::to_string(when), std::to_string(when), label});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to update git server last_check for '" +
                             label + "': " + conn_->last_error());
  }

  return ok;
}

bool Store::update_git_server_admin_token(const std::string &label,
                                          const std::string &token) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot update git server admin token: no database connection");
    }
    return false;
  }

  std::string stored_token = token.empty()
      ? std::string()
      : encrypt_token(token, encryption_salt_);

  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "UPDATE git_servers SET admin_token = ?, updated_at = ? WHERE label = ?;",
      {stored_token, std::to_string(now), label});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to update admin token for git server '" + label +
                    "': " + conn_->last_error());
  }

  return ok;
}

bool Store::remove_git_server(const std::string &label) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot remove git server: no database connection");
    }
    return false;
  }

  bool ok =
      conn_->execute("DELETE FROM git_servers WHERE label = ?;", {label});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to remove git server '" + label +
                             "': " + conn_->last_error());
  }

  return ok;
}

std::vector<std::string>
Store::list_bare_repo_paths_with_prefix(const std::string &server_path_prefix) {
  std::vector<std::string> paths;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot list bare repositories: no database connection");
    }
    return paths;
  }

  if (server_path_prefix.empty()) {
    return paths;
  }

  std::string like_pattern = server_path_prefix;
  if (like_pattern.back() == '/') {
    like_pattern += "%";
  } else {
    like_pattern += "/%";
  }

  auto result = conn_->query(
      "SELECT server_path FROM bare_repos WHERE server_path LIKE ? ORDER BY server_path;",
      {like_pattern});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to list bare repositories: " +
                               conn_->last_error());
    }
    return paths;
  }

  for (const auto &row : result.rows) {
    auto value = row.get("server_path");
    if (value && !value->empty()) {
      paths.push_back(*value);
    }
  }

  return paths;
}

int64_t Store::add_repo_migration(const RepoMigrationRecord &record) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot add repo migration: no database connection");
    }
    return 0;
  }

  int64_t now = now_timestamp();

  std::string sql =
      "INSERT INTO repo_migrations (server_id, project_id, repo_name, "
      "source_path, started_at, status, branch_count, tag_count, size_bytes, "
      "error_message) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

  bool ok = conn_->execute(sql,
                           {std::to_string(record.server_id),
                            record.project_id ? std::to_string(record.project_id)
                                              : std::string(),
                            record.repo_name,
                            record.source_path,
                            std::to_string(record.started_at ? record.started_at : now),
                            record.status.empty() ? "pending" : record.status,
                            std::to_string(record.branch_count),
                            std::to_string(record.tag_count),
                            std::to_string(record.size_bytes),
                            record.error_message});

  if (!ok) {
    if (log_) {
      log_->error("Nexus", "Failed to add repo migration: " + conn_->last_error());
    }
    return 0;
  }

  return conn_->last_insert_id();
}

bool Store::update_repo_migration_status(int64_t migration_id,
                                         const std::string &status,
                                         const std::string &error_message) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot update repo migration: no database connection");
    }
    return false;
  }

  bool ok = conn_->execute(
      "UPDATE repo_migrations SET status = ?, error_message = ? WHERE id = ?;",
      {status, error_message, std::to_string(migration_id)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to update repo migration status: " +
                             conn_->last_error());
  }

  return ok;
}

bool Store::mark_repo_migration_completed(int64_t migration_id,
                                          int64_t completed_at) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot mark repo migration complete: no database connection");
    }
    return false;
  }

  int64_t when = completed_at != 0 ? completed_at : now_timestamp();

  bool ok = conn_->execute(
      "UPDATE repo_migrations SET completed_at = ?, status = 'completed' WHERE id = ?;",
      {std::to_string(when), std::to_string(migration_id)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to mark repo migration complete: " +
                             conn_->last_error());
  }

  return ok;
}

std::vector<RepoMigrationRecord>
Store::list_repo_migrations_for_server(int64_t server_id, int limit) {
  std::vector<RepoMigrationRecord> records;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot list repo migrations: no database connection");
    }
    return records;
  }

  auto result = conn_->query(
      "SELECT id, server_id, project_id, repo_name, source_path, started_at, "
      "completed_at, status, branch_count, tag_count, size_bytes, error_message "
      "FROM repo_migrations WHERE server_id = ? ORDER BY started_at DESC LIMIT ?;",
      {std::to_string(server_id), std::to_string(limit)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to list repo migrations: " +
                               conn_->last_error());
    }
    return records;
  }

  for (const auto &row : result.rows) {
    RepoMigrationRecord rec;
    rec.id = row.get_int("id").value_or(0);
    rec.server_id = row.get_int("server_id").value_or(0);
    rec.project_id = row.get_int("project_id").value_or(0);
    rec.repo_name = row.get("repo_name").value_or("");
    rec.source_path = row.get("source_path").value_or("");
    rec.started_at = row.get_int("started_at").value_or(0);
    rec.completed_at = row.get_int("completed_at").value_or(0);
    rec.status = row.get("status").value_or("");
    rec.branch_count = static_cast<int>(row.get_int("branch_count").value_or(0));
    rec.tag_count = static_cast<int>(row.get_int("tag_count").value_or(0));
    rec.size_bytes = row.get_int("size_bytes").value_or(0);
    rec.error_message = row.get("error_message").value_or("");
    records.push_back(std::move(rec));
  }

  return records;
}

int64_t Store::add_git_server_health(const GitServerHealthRecord &record) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot add git server health: no database connection");
    }
    return 0;
  }

  std::string sql =
      "INSERT INTO git_server_health (server_id, timestamp, status, "
      "web_ui_reachable, http_clone_works, ssh_push_works, service_status, "
      "repo_count, total_size_bytes, disk_used_pct, disk_free_bytes, notes) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

  bool ok = conn_->execute(sql,
                           {std::to_string(record.server_id),
                            std::to_string(record.timestamp),
                            record.status,
                            record.web_ui_reachable ? "1" : "0",
                            record.http_clone_works ? "1" : "0",
                            record.ssh_push_works ? "1" : "0",
                            record.service_status_json,
                            std::to_string(record.repo_count),
                            std::to_string(record.total_size_bytes),
                            std::to_string(record.disk_used_pct),
                            std::to_string(record.disk_free_bytes),
                            record.notes_json});

  if (!ok) {
    if (log_) {
      log_->error("Nexus", "Failed to add git server health: " +
                               conn_->last_error());
    }
    return 0;
  }

  return conn_->last_insert_id();
}

std::vector<GitServerHealthRecord>
Store::list_git_server_health(int64_t server_id, int limit) {
  std::vector<GitServerHealthRecord> records;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot list git server health: no database connection");
    }
    return records;
  }

  auto result = conn_->query(
      "SELECT id, server_id, timestamp, status, web_ui_reachable, "
      "http_clone_works, ssh_push_works, service_status, repo_count, "
      "total_size_bytes, disk_used_pct, disk_free_bytes, notes "
      "FROM git_server_health WHERE server_id = ? ORDER BY timestamp DESC LIMIT ?;",
      {std::to_string(server_id), std::to_string(limit)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to list git server health: " +
                               conn_->last_error());
    }
    return records;
  }

  for (const auto &row : result.rows) {
    GitServerHealthRecord rec;
    rec.id = row.get_int("id").value_or(0);
    rec.server_id = row.get_int("server_id").value_or(0);
    rec.timestamp = row.get_int("timestamp").value_or(0);
    rec.status = row.get("status").value_or("");
    rec.web_ui_reachable = row.get_int("web_ui_reachable").value_or(0) != 0;
    rec.http_clone_works = row.get_int("http_clone_works").value_or(0) != 0;
    rec.ssh_push_works = row.get_int("ssh_push_works").value_or(0) != 0;
    rec.service_status_json = row.get("service_status").value_or("");
    rec.repo_count = static_cast<int>(row.get_int("repo_count").value_or(0));
    rec.total_size_bytes = row.get_int("total_size_bytes").value_or(0);
    rec.disk_used_pct = static_cast<int>(row.get_int("disk_used_pct").value_or(0));
    rec.disk_free_bytes = row.get_int("disk_free_bytes").value_or(0);
    rec.notes_json = row.get("notes").value_or("");
    records.push_back(std::move(rec));
  }

  return records;
}

// ===== Workspace Snapshots =====

int64_t Store::add_workspace_snapshot(
    int64_t project_id, int64_t brain_snapshot_id, const std::string &label,
    const std::string &trigger_type, const std::string &build_dir_hash,
    const std::string &deps_manifest_hash, const std::string &env_snapshot_json,
    const std::string &system_info_json, bool is_clean_build,
    const std::string &git_commit, const std::string &git_branch) {

  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot add workspace snapshot: no database connection");
    }
    return 0;
  }

  int64_t now = now_timestamp();

  // Handle NULL brain_snapshot_id
  std::string sql;
  std::vector<std::string> params;

  if (brain_snapshot_id > 0) {
    sql = "INSERT INTO workspace_snapshots "
          "(project_id, brain_snapshot_id, label, trigger_type, timestamp, "
          "build_dir_hash, deps_manifest_hash, env_snapshot, system_info, "
          "is_clean_build, git_commit, git_branch, created_at) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    params = {std::to_string(project_id), std::to_string(brain_snapshot_id),
              label, trigger_type, std::to_string(now), build_dir_hash,
              deps_manifest_hash, env_snapshot_json, system_info_json,
              is_clean_build ? "1" : "0", git_commit, git_branch,
              std::to_string(now)};
  } else {
    sql = "INSERT INTO workspace_snapshots "
          "(project_id, brain_snapshot_id, label, trigger_type, timestamp, "
          "build_dir_hash, deps_manifest_hash, env_snapshot, system_info, "
          "is_clean_build, git_commit, git_branch, created_at) "
          "VALUES (?, NULL, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    params = {std::to_string(project_id), label, trigger_type,
              std::to_string(now), build_dir_hash, deps_manifest_hash,
              env_snapshot_json, system_info_json,
              is_clean_build ? "1" : "0", git_commit, git_branch,
              std::to_string(now)};
  }

  bool ok = conn_->execute(sql, params);

  if (!ok) {
    if (log_) {
      log_->error("Nexus", "Failed to add workspace snapshot: " +
                               conn_->last_error());
    }
    return 0;
  }

  return conn_->last_insert_id();
}

std::optional<std::map<std::string, std::string>>
Store::get_workspace_snapshot(int64_t snapshot_id) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot get workspace snapshot: no database connection");
    }
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, project_id, brain_snapshot_id, label, trigger_type, "
      "timestamp, build_dir_hash, deps_manifest_hash, env_snapshot, "
      "system_info, restore_count, is_clean_build, git_commit, git_branch, "
      "created_at, tags "
      "FROM workspace_snapshots WHERE id = ? LIMIT 1;",
      {std::to_string(snapshot_id)});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> snap;
  snap["id"] = std::to_string(row.get_int("id").value_or(0));
  snap["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
  snap["brain_snapshot_id"] = std::to_string(row.get_int("brain_snapshot_id").value_or(0));
  snap["label"] = row.get("label").value_or("");
  snap["trigger_type"] = row.get("trigger_type").value_or("");
  snap["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
  snap["build_dir_hash"] = row.get("build_dir_hash").value_or("");
  snap["deps_manifest_hash"] = row.get("deps_manifest_hash").value_or("");
  snap["env_snapshot"] = row.get("env_snapshot").value_or("");
  snap["system_info"] = row.get("system_info").value_or("");
  snap["restore_count"] = std::to_string(row.get_int("restore_count").value_or(0));
  snap["is_clean_build"] = std::to_string(row.get_int("is_clean_build").value_or(0));
  snap["git_commit"] = row.get("git_commit").value_or("");
  snap["git_branch"] = row.get("git_branch").value_or("");
  snap["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
  snap["tags"] = row.get("tags").value_or("");
  return snap;
}

std::optional<std::map<std::string, std::string>>
Store::latest_workspace_snapshot(int64_t project_id) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot get latest workspace snapshot: no database "
                           "connection");
    }
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, project_id, brain_snapshot_id, label, trigger_type, "
      "timestamp, build_dir_hash, deps_manifest_hash, env_snapshot, "
      "system_info, restore_count, is_clean_build, git_commit, git_branch, "
      "created_at, tags "
      "FROM workspace_snapshots WHERE project_id = ? "
      "ORDER BY timestamp DESC LIMIT 1;",
      {std::to_string(project_id)});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> snap;
  snap["id"] = std::to_string(row.get_int("id").value_or(0));
  snap["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
  snap["brain_snapshot_id"] = std::to_string(row.get_int("brain_snapshot_id").value_or(0));
  snap["label"] = row.get("label").value_or("");
  snap["trigger_type"] = row.get("trigger_type").value_or("");
  snap["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
  snap["build_dir_hash"] = row.get("build_dir_hash").value_or("");
  snap["deps_manifest_hash"] = row.get("deps_manifest_hash").value_or("");
  snap["env_snapshot"] = row.get("env_snapshot").value_or("");
  snap["system_info"] = row.get("system_info").value_or("");
  snap["restore_count"] = std::to_string(row.get_int("restore_count").value_or(0));
  snap["is_clean_build"] = std::to_string(row.get_int("is_clean_build").value_or(0));
  snap["git_commit"] = row.get("git_commit").value_or("");
  snap["git_branch"] = row.get("git_branch").value_or("");
  snap["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
  snap["tags"] = row.get("tags").value_or("");
  return snap;
}

std::vector<std::map<std::string, std::string>>
Store::list_workspace_snapshots(int64_t project_id, int limit) {
  std::vector<std::map<std::string, std::string>> snapshots;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot list workspace snapshots: no database "
                           "connection");
    }
    return snapshots;
  }

  auto result = conn_->query(
      "SELECT id, project_id, brain_snapshot_id, label, trigger_type, "
      "timestamp, build_dir_hash, deps_manifest_hash, restore_count, "
      "is_clean_build, git_commit, git_branch, created_at, tags "
      "FROM workspace_snapshots WHERE project_id = ? "
      "ORDER BY timestamp DESC LIMIT ?;",
      {std::to_string(project_id), std::to_string(limit)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to list workspace snapshots: " +
                               conn_->last_error());
    }
    return snapshots;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> snap;
    snap["id"] = std::to_string(row.get_int("id").value_or(0));
    snap["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    snap["brain_snapshot_id"] = std::to_string(row.get_int("brain_snapshot_id").value_or(0));
    snap["label"] = row.get("label").value_or("");
    snap["trigger_type"] = row.get("trigger_type").value_or("");
    snap["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
    snap["build_dir_hash"] = row.get("build_dir_hash").value_or("");
    snap["deps_manifest_hash"] = row.get("deps_manifest_hash").value_or("");
    snap["restore_count"] = std::to_string(row.get_int("restore_count").value_or(0));
    snap["is_clean_build"] = std::to_string(row.get_int("is_clean_build").value_or(0));
    snap["git_commit"] = row.get("git_commit").value_or("");
    snap["git_branch"] = row.get("git_branch").value_or("");
    snap["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
    snap["tags"] = row.get("tags").value_or("");
    snapshots.push_back(snap);
  }

  return snapshots;
}

bool Store::update_workspace_snapshot_restore_count(int64_t snapshot_id) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot update restore count: no database "
                           "connection");
    }
    return false;
  }

  bool ok = conn_->execute(
      "UPDATE workspace_snapshots SET restore_count = restore_count + 1 "
      "WHERE id = ?;",
      {std::to_string(snapshot_id)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to update restore count: " +
                             conn_->last_error());
  }

  return ok;
}

// ===== Workspace Files =====

void Store::add_workspace_file(int64_t snapshot_id,
                                const std::string &file_path,
                                const std::string &file_type,
                                const std::string &file_hash, int64_t file_size,
                                int64_t mtime) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot add workspace file: no database connection");
    }
    return;
  }

  std::string sql =
      "INSERT INTO workspace_files "
      "(snapshot_id, file_path, file_type, file_hash, file_size, mtime) "
      "VALUES (?, ?, ?, ?, ?, ?);";

  bool ok = conn_->execute(sql,
                           {std::to_string(snapshot_id), file_path, file_type,
                            file_hash, std::to_string(file_size),
                            std::to_string(mtime)});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to add workspace file: " + conn_->last_error());
  }
}

std::vector<std::map<std::string, std::string>>
Store::get_workspace_files(int64_t snapshot_id) {
  std::vector<std::map<std::string, std::string>> files;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot get workspace files: no database connection");
    }
    return files;
  }

  auto result = conn_->query(
      "SELECT id, snapshot_id, file_path, file_type, file_hash, file_size, "
      "mtime "
      "FROM workspace_files WHERE snapshot_id = ? ORDER BY file_path;",
      {std::to_string(snapshot_id)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to get workspace files: " + conn_->last_error());
    }
    return files;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> file;
    file["id"] = std::to_string(row.get_int("id").value_or(0));
    file["snapshot_id"] = std::to_string(row.get_int("snapshot_id").value_or(0));
    file["file_path"] = row.get("file_path").value_or("");
    file["file_type"] = row.get("file_type").value_or("");
    file["file_hash"] = row.get("file_hash").value_or("");
    file["file_size"] = std::to_string(row.get_int("file_size").value_or(0));
    file["mtime"] = std::to_string(row.get_int("mtime").value_or(0));
    files.push_back(file);
  }

  return files;
}

bool Store::delete_workspace_snapshot(int64_t snapshot_id) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot delete workspace snapshot: no database connection");
    }
    return false;
  }

  bool ok = conn_->execute("DELETE FROM workspace_snapshots WHERE id = ?;",
                           {std::to_string(snapshot_id)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to delete workspace snapshot: " +
                             conn_->last_error());
  }

  return ok;
}

// ===== Workspace Restores =====

int64_t Store::begin_workspace_restore(int64_t project_id, int64_t snapshot_id,
                                        const std::string &restore_type,
                                        const std::string &reason) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot begin workspace restore: no database connection");
    }
    return 0;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO workspace_restores "
      "(project_id, from_snapshot_id, restore_type, timestamp, reason) "
      "VALUES (?, ?, ?, ?, ?);";

  bool ok = conn_->execute(sql,
                           {std::to_string(project_id),
                            std::to_string(snapshot_id), restore_type,
                            std::to_string(now), reason});

  if (!ok) {
    if (log_) {
      log_->error("Nexus", "Failed to begin workspace restore: " +
                               conn_->last_error());
    }
    return 0;
  }

  return conn_->last_insert_id();
}

void Store::finish_workspace_restore(int64_t restore_id, bool success,
                                      int files_restored,
                                      int64_t duration_ms) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot finish workspace restore: no database connection");
    }
    return;
  }

  std::string sql =
      "UPDATE workspace_restores SET success = ?, files_restored = ?, "
      "duration_ms = ? WHERE id = ?;";

  bool ok = conn_->execute(sql,
                           {success ? "1" : "0",
                            std::to_string(files_restored),
                            std::to_string(duration_ms),
                            std::to_string(restore_id)});

  if (!ok && log_) {
    log_->error("Nexus", "Failed to finish workspace restore: " +
                             conn_->last_error());
  }
}

std::vector<std::map<std::string, std::string>>
Store::list_workspace_restores(int64_t project_id, int limit) {
  std::vector<std::map<std::string, std::string>> restores;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot list workspace restores: no database "
                           "connection");
    }
    return restores;
  }

  auto result = conn_->query(
      "SELECT id, project_id, from_snapshot_id, restore_type, timestamp, "
      "reason, success, files_restored, duration_ms "
      "FROM workspace_restores WHERE project_id = ? "
      "ORDER BY timestamp DESC LIMIT ?;",
      {std::to_string(project_id), std::to_string(limit)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to list workspace restores: " +
                               conn_->last_error());
    }
    return restores;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> restore;
    restore["id"] = std::to_string(row.get_int("id").value_or(0));
    restore["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    restore["from_snapshot_id"] = std::to_string(row.get_int("from_snapshot_id").value_or(0));
    restore["restore_type"] = row.get("restore_type").value_or("");
    restore["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
    restore["reason"] = row.get("reason").value_or("");
    restore["success"] = std::to_string(row.get_int("success").value_or(0));
    restore["files_restored"] = std::to_string(row.get_int("files_restored").value_or(0));
    restore["duration_ms"] = std::to_string(row.get_int("duration_ms").value_or(0));
    restores.push_back(restore);
  }

  return restores;
}

// ===== Workspace Tags =====

bool Store::tag_workspace_snapshot(int64_t project_id, int64_t snapshot_id,
                                    const std::string &tag_name,
                                    const std::string &description) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot tag workspace snapshot: no database connection");
    }
    return false;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO workspace_tags "
      "(project_id, snapshot_id, tag_name, description, created_at) "
      "VALUES (?, ?, ?, ?, ?) "
      "ON CONFLICT(project_id, tag_name) DO UPDATE SET "
      "snapshot_id = excluded.snapshot_id, "
      "description = excluded.description;";

  bool ok = conn_->execute(sql,
                           {std::to_string(project_id),
                            std::to_string(snapshot_id), tag_name, description,
                            std::to_string(now)});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to tag workspace snapshot: " + conn_->last_error());
  }

  return ok;
}

std::optional<int64_t> Store::get_snapshot_by_tag(int64_t project_id,
                                                   const std::string &tag_name) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot get snapshot by tag: no database connection");
    }
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT snapshot_id FROM workspace_tags "
      "WHERE project_id = ? AND tag_name = ? LIMIT 1;",
      {std::to_string(project_id), tag_name});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  return result.rows[0].get_int("snapshot_id");
}

bool Store::untag_workspace_snapshot(int64_t project_id,
                                      const std::string &tag_name) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot untag workspace snapshot: no database connection");
    }
    return false;
  }

  bool ok = conn_->execute(
      "DELETE FROM workspace_tags WHERE project_id = ? AND tag_name = ?;",
      {std::to_string(project_id), tag_name});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to untag workspace snapshot: " + conn_->last_error());
  }

  return ok;
}

std::vector<std::map<std::string, std::string>>
Store::list_workspace_tags(int64_t project_id) {
  std::vector<std::map<std::string, std::string>> tags;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot list workspace tags: no database connection");
    }
    return tags;
  }

  auto result = conn_->query(
      "SELECT id, project_id, snapshot_id, tag_name, description, created_at "
      "FROM workspace_tags WHERE project_id = ? ORDER BY tag_name;",
      {std::to_string(project_id)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to list workspace tags: " + conn_->last_error());
    }
    return tags;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> tag;
    tag["id"] = std::to_string(row.get_int("id").value_or(0));
    tag["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    tag["snapshot_id"] = std::to_string(row.get_int("snapshot_id").value_or(0));
    tag["tag_name"] = row.get("tag_name").value_or("");
    tag["description"] = row.get("description").value_or("");
    tag["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
    tags.push_back(tag);
  }

  return tags;
}

// ===== Workspace Failures =====

void Store::record_workspace_failure(
    int64_t project_id, const std::string &failure_type,
    const std::string &error_signature, const std::string &error_message,
    int64_t before_snapshot_id, int64_t after_snapshot_id,
    const std::string &changed_files_json,
    const std::string &changed_deps_json) {

  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot record workspace failure: no database connection");
    }
    return;
  }

  int64_t now = now_timestamp();
  std::string before_str =
      before_snapshot_id > 0 ? std::to_string(before_snapshot_id) : "";
  std::string after_str =
      after_snapshot_id > 0 ? std::to_string(after_snapshot_id) : "";

  std::string sql =
      "INSERT INTO workspace_failures "
      "(project_id, failure_type, error_signature, error_message, "
      "before_snapshot_id, after_snapshot_id, changed_files, changed_deps, "
      "timestamp) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

  bool ok = conn_->execute(
      sql, {std::to_string(project_id), failure_type, error_signature,
            error_message, before_str, after_str, changed_files_json,
            changed_deps_json, std::to_string(now)});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to record workspace failure: " + conn_->last_error());
  }
}

std::vector<std::map<std::string, std::string>>
Store::find_similar_failures(int64_t project_id,
                              const std::string &error_signature, int limit) {
  std::vector<std::map<std::string, std::string>> failures;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus",
                  "Cannot find similar failures: no database connection");
    }
    return failures;
  }

  auto result = conn_->query(
      "SELECT id, project_id, failure_type, error_signature, error_message, "
      "before_snapshot_id, after_snapshot_id, changed_files, changed_deps, "
      "resolved, resolution_type, resolution_notes, timestamp "
      "FROM workspace_failures "
      "WHERE project_id = ? AND error_signature = ? "
      "ORDER BY timestamp DESC LIMIT ?;",
      {std::to_string(project_id), error_signature, std::to_string(limit)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to find similar failures: " + conn_->last_error());
    }
    return failures;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> failure;
    failure["id"] = std::to_string(row.get_int("id").value_or(0));
    failure["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    failure["failure_type"] = row.get("failure_type").value_or("");
    failure["error_signature"] = row.get("error_signature").value_or("");
    failure["error_message"] = row.get("error_message").value_or("");
    failure["before_snapshot_id"] = std::to_string(row.get_int("before_snapshot_id").value_or(0));
    failure["after_snapshot_id"] = std::to_string(row.get_int("after_snapshot_id").value_or(0));
    failure["changed_files"] = row.get("changed_files").value_or("");
    failure["changed_deps"] = row.get("changed_deps").value_or("");
    failure["resolved"] = std::to_string(row.get_int("resolved").value_or(0));
    failure["resolution_type"] = row.get("resolution_type").value_or("");
    failure["resolution_notes"] = row.get("resolution_notes").value_or("");
    failure["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
    failures.push_back(failure);
  }

  return failures;
}

// ===== Brain Failure Learning =====

int64_t Store::record_failure_with_context(
    int64_t project_id, const std::string &failure_type,
    const std::string &error_signature, const std::string &error_message,
    const std::string &error_location, const std::string &command_executed,
    int exit_code, int64_t before_snapshot_id, int64_t after_snapshot_id,
    const std::string &changed_files_json, const std::string &changed_deps_json,
    const std::string &changed_env_json, const std::string &changed_system_json,
    const std::string &severity, const std::string &tags_json) {

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot record failure: no database connection");
    }
    return 0;
  }

  int64_t now = now_timestamp();

  std::string before_str =
      before_snapshot_id > 0 ? std::to_string(before_snapshot_id) : "";
  std::string after_str =
      after_snapshot_id > 0 ? std::to_string(after_snapshot_id) : "";

  std::string sql =
      "INSERT INTO workspace_failures "
      "(project_id, failure_type, error_signature, error_message, "
      "error_location, command_executed, exit_code, "
      "before_snapshot_id, after_snapshot_id, "
      "changed_files, changed_deps, changed_env, changed_system, "
      "severity, tags_json, timestamp) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

  bool ok = conn_->execute(
      sql, {std::to_string(project_id), failure_type, error_signature,
            error_message, error_location, command_executed,
            std::to_string(exit_code), before_str, after_str, changed_files_json,
            changed_deps_json, changed_env_json, changed_system_json, severity,
            tags_json, std::to_string(now)});

  if (!ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to record failure: " + conn_->last_error());
    }
    return 0;
  }

  return conn_->last_insert_id();
}

std::optional<std::map<std::string, std::string>>
Store::get_failure(int64_t failure_id) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot get failure: no database connection");
    }
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, project_id, failure_type, error_signature, error_message, "
      "error_location, command_executed, exit_code, "
      "before_snapshot_id, after_snapshot_id, "
      "changed_files, changed_deps, changed_env, changed_system, "
      "resolved, resolved_at, resolution_type, resolution_snapshot_id, "
      "resolution_notes, resolution_success, "
      "severity, tags_json, timestamp "
      "FROM workspace_failures WHERE id = ?;",
      {std::to_string(failure_id)});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> failure;
  failure["id"] = std::to_string(row.get_int("id").value_or(0));
  failure["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
  failure["failure_type"] = row.get("failure_type").value_or("");
  failure["error_signature"] = row.get("error_signature").value_or("");
  failure["error_message"] = row.get("error_message").value_or("");
  failure["error_location"] = row.get("error_location").value_or("");
  failure["command_executed"] = row.get("command_executed").value_or("");
  failure["exit_code"] = std::to_string(row.get_int("exit_code").value_or(0));
  failure["before_snapshot_id"] =
      std::to_string(row.get_int("before_snapshot_id").value_or(0));
  failure["after_snapshot_id"] =
      std::to_string(row.get_int("after_snapshot_id").value_or(0));
  failure["changed_files"] = row.get("changed_files").value_or("");
  failure["changed_deps"] = row.get("changed_deps").value_or("");
  failure["changed_env"] = row.get("changed_env").value_or("");
  failure["changed_system"] = row.get("changed_system").value_or("");
  failure["resolved"] = std::to_string(row.get_int("resolved").value_or(0));
  failure["resolved_at"] =
      std::to_string(row.get_int("resolved_at").value_or(0));
  failure["resolution_type"] = row.get("resolution_type").value_or("");
  failure["resolution_snapshot_id"] =
      std::to_string(row.get_int("resolution_snapshot_id").value_or(0));
  failure["resolution_notes"] = row.get("resolution_notes").value_or("");
  failure["resolution_success"] =
      std::to_string(row.get_int("resolution_success").value_or(0));
  failure["severity"] = row.get("severity").value_or("medium");
  failure["tags_json"] = row.get("tags_json").value_or("[]");
  failure["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));

  return failure;
}

std::vector<std::map<std::string, std::string>>
Store::list_failures(int64_t project_id, int limit) {
  std::vector<std::map<std::string, std::string>> failures;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot list failures: no database connection");
    }
    return failures;
  }

  auto result = conn_->query(
      "SELECT id, project_id, failure_type, error_signature, error_message, "
      "error_location, command_executed, exit_code, "
      "before_snapshot_id, after_snapshot_id, "
      "resolved, resolution_type, severity, timestamp "
      "FROM workspace_failures "
      "WHERE project_id = ? "
      "ORDER BY timestamp DESC LIMIT ?;",
      {std::to_string(project_id), std::to_string(limit)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to list failures: " + conn_->last_error());
    }
    return failures;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> failure;
    failure["id"] = std::to_string(row.get_int("id").value_or(0));
    failure["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    failure["failure_type"] = row.get("failure_type").value_or("");
    failure["error_signature"] = row.get("error_signature").value_or("");
    failure["error_message"] = row.get("error_message").value_or("");
    failure["error_location"] = row.get("error_location").value_or("");
    failure["command_executed"] = row.get("command_executed").value_or("");
    failure["exit_code"] = std::to_string(row.get_int("exit_code").value_or(0));
    failure["before_snapshot_id"] =
        std::to_string(row.get_int("before_snapshot_id").value_or(0));
    failure["after_snapshot_id"] =
        std::to_string(row.get_int("after_snapshot_id").value_or(0));
    failure["resolved"] = std::to_string(row.get_int("resolved").value_or(0));
    failure["resolution_type"] = row.get("resolution_type").value_or("");
    failure["severity"] = row.get("severity").value_or("medium");
    failure["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
    failures.push_back(failure);
  }

  return failures;
}

std::vector<std::map<std::string, std::string>>
Store::list_unresolved_failures(int64_t project_id, int limit) {
  std::vector<std::map<std::string, std::string>> failures;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot list unresolved failures: no database connection");
    }
    return failures;
  }

  auto result = conn_->query(
      "SELECT id, project_id, failure_type, error_signature, error_message, "
      "error_location, command_executed, exit_code, "
      "before_snapshot_id, after_snapshot_id, "
      "severity, timestamp "
      "FROM workspace_failures "
      "WHERE project_id = ? AND resolved = 0 "
      "ORDER BY timestamp DESC LIMIT ?;",
      {std::to_string(project_id), std::to_string(limit)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to list unresolved failures: " + conn_->last_error());
    }
    return failures;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> failure;
    failure["id"] = std::to_string(row.get_int("id").value_or(0));
    failure["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    failure["failure_type"] = row.get("failure_type").value_or("");
    failure["error_signature"] = row.get("error_signature").value_or("");
    failure["error_message"] = row.get("error_message").value_or("");
    failure["error_location"] = row.get("error_location").value_or("");
    failure["command_executed"] = row.get("command_executed").value_or("");
    failure["exit_code"] = std::to_string(row.get_int("exit_code").value_or(0));
    failure["before_snapshot_id"] =
        std::to_string(row.get_int("before_snapshot_id").value_or(0));
    failure["after_snapshot_id"] =
        std::to_string(row.get_int("after_snapshot_id").value_or(0));
    failure["severity"] = row.get("severity").value_or("medium");
    failure["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
    failures.push_back(failure);
  }

  return failures;
}

bool Store::update_failure_resolution(int64_t failure_id,
                                       const std::string &resolution_type,
                                       int64_t resolution_snapshot_id,
                                       const std::string &notes,
                                       bool success) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot update failure resolution: no database connection");
    }
    return false;
  }

  int64_t now = now_timestamp();
  std::string resolution_snapshot_str =
      resolution_snapshot_id > 0 ? std::to_string(resolution_snapshot_id) : "";

  std::string sql =
      "UPDATE workspace_failures "
      "SET resolved = 1, resolved_at = ?, resolution_type = ?, "
      "resolution_snapshot_id = ?, resolution_notes = ?, resolution_success = ? "
      "WHERE id = ?;";

  bool ok = conn_->execute(sql, {std::to_string(now), resolution_type,
                                  resolution_snapshot_str, notes,
                                  success ? "1" : "0",
                                  std::to_string(failure_id)});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to update failure resolution: " + conn_->last_error());
  }

  return ok;
}

// ===== Failure Patterns =====

int64_t Store::record_pattern(int64_t project_id,
                               const std::string &pattern_signature,
                               const std::string &pattern_name,
                               const std::string &failure_type,
                               const std::string &error_regex,
                               const std::string &trigger_conditions_json) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot record pattern: no database connection");
    }
    return 0;
  }

  int64_t now = now_timestamp();

  std::string sql =
      "INSERT INTO brain_failure_patterns "
      "(project_id, pattern_signature, pattern_name, failure_type, "
      "error_regex, trigger_conditions_json, occurrence_count, "
      "first_seen, last_seen) "
      "VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?);";

  bool ok = conn_->execute(
      sql, {std::to_string(project_id), pattern_signature, pattern_name,
            failure_type, error_regex, trigger_conditions_json,
            std::to_string(now), std::to_string(now)});

  if (!ok) {
    if (log_) {
      log_->error("Nexus", "Failed to record pattern: " + conn_->last_error());
    }
    return 0;
  }

  return conn_->last_insert_id();
}

std::optional<std::map<std::string, std::string>>
Store::get_pattern_by_signature(int64_t project_id,
                                const std::string &pattern_signature) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot get pattern: no database connection");
    }
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, project_id, pattern_signature, pattern_name, failure_type, "
      "error_regex, trigger_conditions_json, occurrence_count, "
      "first_seen, last_seen, failure_ids_json, resolution_strategies_json "
      "FROM brain_failure_patterns "
      "WHERE project_id = ? AND pattern_signature = ?;",
      {std::to_string(project_id), pattern_signature});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> pattern;
  pattern["id"] = std::to_string(row.get_int("id").value_or(0));
  pattern["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
  pattern["pattern_signature"] = row.get("pattern_signature").value_or("");
  pattern["pattern_name"] = row.get("pattern_name").value_or("");
  pattern["failure_type"] = row.get("failure_type").value_or("");
  pattern["error_regex"] = row.get("error_regex").value_or("");
  pattern["trigger_conditions_json"] =
      row.get("trigger_conditions_json").value_or("");
  pattern["occurrence_count"] =
      std::to_string(row.get_int("occurrence_count").value_or(0));
  pattern["first_seen"] = std::to_string(row.get_int("first_seen").value_or(0));
  pattern["last_seen"] = std::to_string(row.get_int("last_seen").value_or(0));
  pattern["failure_ids_json"] = row.get("failure_ids_json").value_or("");
  pattern["resolution_strategies_json"] =
      row.get("resolution_strategies_json").value_or("");

  return pattern;
}

std::optional<std::map<std::string, std::string>>
Store::get_pattern(int64_t pattern_id) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot get pattern: no database connection");
    }
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, project_id, pattern_signature, pattern_name, failure_type, "
      "error_regex, trigger_conditions_json, occurrence_count, "
      "first_seen, last_seen, failure_ids_json, resolution_strategies_json "
      "FROM brain_failure_patterns WHERE id = ?;",
      {std::to_string(pattern_id)});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> pattern;
  pattern["id"] = std::to_string(row.get_int("id").value_or(0));
  pattern["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
  pattern["pattern_signature"] = row.get("pattern_signature").value_or("");
  pattern["pattern_name"] = row.get("pattern_name").value_or("");
  pattern["failure_type"] = row.get("failure_type").value_or("");
  pattern["error_regex"] = row.get("error_regex").value_or("");
  pattern["trigger_conditions_json"] =
      row.get("trigger_conditions_json").value_or("");
  pattern["occurrence_count"] =
      std::to_string(row.get_int("occurrence_count").value_or(0));
  pattern["first_seen"] = std::to_string(row.get_int("first_seen").value_or(0));
  pattern["last_seen"] = std::to_string(row.get_int("last_seen").value_or(0));
  pattern["failure_ids_json"] = row.get("failure_ids_json").value_or("");
  pattern["resolution_strategies_json"] =
      row.get("resolution_strategies_json").value_or("");

  return pattern;
}

std::vector<std::map<std::string, std::string>>
Store::list_patterns(int64_t project_id, int limit) {
  std::vector<std::map<std::string, std::string>> patterns;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot list patterns: no database connection");
    }
    return patterns;
  }

  auto result = conn_->query(
      "SELECT id, project_id, pattern_signature, pattern_name, failure_type, "
      "occurrence_count, first_seen, last_seen "
      "FROM brain_failure_patterns "
      "WHERE project_id = ? "
      "ORDER BY occurrence_count DESC LIMIT ?;",
      {std::to_string(project_id), std::to_string(limit)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus", "Failed to list patterns: " + conn_->last_error());
    }
    return patterns;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> pattern;
    pattern["id"] = std::to_string(row.get_int("id").value_or(0));
    pattern["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    pattern["pattern_signature"] = row.get("pattern_signature").value_or("");
    pattern["pattern_name"] = row.get("pattern_name").value_or("");
    pattern["failure_type"] = row.get("failure_type").value_or("");
    pattern["occurrence_count"] =
        std::to_string(row.get_int("occurrence_count").value_or(0));
    pattern["first_seen"] =
        std::to_string(row.get_int("first_seen").value_or(0));
    pattern["last_seen"] = std::to_string(row.get_int("last_seen").value_or(0));
    patterns.push_back(pattern);
  }

  return patterns;
}

bool Store::update_pattern_statistics(int64_t pattern_id, int occurrence_delta) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot update pattern statistics: no database connection");
    }
    return false;
  }

  int64_t now = now_timestamp();

  std::string sql =
      "UPDATE brain_failure_patterns "
      "SET occurrence_count = occurrence_count + ?, last_seen = ? "
      "WHERE id = ?;";

  bool ok = conn_->execute(
      sql, {std::to_string(occurrence_delta), std::to_string(now),
            std::to_string(pattern_id)});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to update pattern statistics: " + conn_->last_error());
  }

  return ok;
}

bool Store::link_failure_to_pattern(int64_t /*failure_id*/, int64_t pattern_id) {
  // This is stored in the failure_ids_json field of the pattern
  // For now, we'll implement a simple approach
  // In a full implementation, we'd parse the JSON, add the ID, and update
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot link failure to pattern: no database connection");
    }
    return false;
  }

  // For now, just update the pattern statistics
  return update_pattern_statistics(pattern_id, 0);
}

// ===== Recovery Actions =====

int64_t Store::add_recovery_action(int64_t pattern_id, int64_t failure_id,
                                    const std::string &action_type,
                                    const std::string &action_params_json,
                                    const std::string &description,
                                    bool requires_confirmation) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot add recovery action: no database connection");
    }
    return 0;
  }

  int64_t now = now_timestamp();

  std::string pattern_str = pattern_id > 0 ? std::to_string(pattern_id) : "";
  std::string failure_str = failure_id > 0 ? std::to_string(failure_id) : "";

  std::string sql =
      "INSERT INTO brain_recovery_actions "
      "(pattern_id, failure_id, action_type, action_params_json, "
      "action_description, requires_user_confirmation, created_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?);";

  bool ok = conn_->execute(
      sql, {pattern_str, failure_str, action_type, action_params_json,
            description, requires_confirmation ? "1" : "0",
            std::to_string(now)});

  if (!ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to add recovery action: " + conn_->last_error());
    }
    return 0;
  }

  return conn_->last_insert_id();
}

std::vector<std::map<std::string, std::string>>
Store::get_recovery_actions_for_pattern(int64_t pattern_id) {
  std::vector<std::map<std::string, std::string>> actions;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot get recovery actions: no database connection");
    }
    return actions;
  }

  auto result = conn_->query(
      "SELECT id, pattern_id, failure_id, action_type, action_params_json, "
      "action_description, attempted_count, success_count, failure_count, "
      "avg_execution_time_ms, confidence_score, requires_user_confirmation "
      "FROM brain_recovery_actions "
      "WHERE pattern_id = ? "
      "ORDER BY confidence_score DESC;",
      {std::to_string(pattern_id)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to get recovery actions: " + conn_->last_error());
    }
    return actions;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> action;
    action["id"] = std::to_string(row.get_int("id").value_or(0));
    action["pattern_id"] = std::to_string(row.get_int("pattern_id").value_or(0));
    action["failure_id"] = std::to_string(row.get_int("failure_id").value_or(0));
    action["action_type"] = row.get("action_type").value_or("");
    action["action_params_json"] = row.get("action_params_json").value_or("");
    action["action_description"] = row.get("action_description").value_or("");
    action["attempted_count"] =
        std::to_string(row.get_int("attempted_count").value_or(0));
    action["success_count"] =
        std::to_string(row.get_int("success_count").value_or(0));
    action["failure_count"] =
        std::to_string(row.get_int("failure_count").value_or(0));
    action["avg_execution_time_ms"] =
        std::to_string(row.get_int("avg_execution_time_ms").value_or(0));
    action["confidence_score"] = row.get("confidence_score").value_or("0.0");
    action["requires_user_confirmation"] =
        std::to_string(row.get_int("requires_user_confirmation").value_or(1));
    actions.push_back(action);
  }

  return actions;
}

std::vector<std::map<std::string, std::string>>
Store::get_recovery_actions_for_failure(int64_t failure_id) {
  std::vector<std::map<std::string, std::string>> actions;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot get recovery actions: no database connection");
    }
    return actions;
  }

  auto result = conn_->query(
      "SELECT id, pattern_id, failure_id, action_type, action_params_json, "
      "action_description, attempted_count, success_count, failure_count, "
      "avg_execution_time_ms, confidence_score, requires_user_confirmation "
      "FROM brain_recovery_actions "
      "WHERE failure_id = ? "
      "ORDER BY confidence_score DESC;",
      {std::to_string(failure_id)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to get recovery actions: " + conn_->last_error());
    }
    return actions;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> action;
    action["id"] = std::to_string(row.get_int("id").value_or(0));
    action["pattern_id"] = std::to_string(row.get_int("pattern_id").value_or(0));
    action["failure_id"] = std::to_string(row.get_int("failure_id").value_or(0));
    action["action_type"] = row.get("action_type").value_or("");
    action["action_params_json"] = row.get("action_params_json").value_or("");
    action["action_description"] = row.get("action_description").value_or("");
    action["attempted_count"] =
        std::to_string(row.get_int("attempted_count").value_or(0));
    action["success_count"] =
        std::to_string(row.get_int("success_count").value_or(0));
    action["failure_count"] =
        std::to_string(row.get_int("failure_count").value_or(0));
    action["avg_execution_time_ms"] =
        std::to_string(row.get_int("avg_execution_time_ms").value_or(0));
    action["confidence_score"] = row.get("confidence_score").value_or("0.0");
    action["requires_user_confirmation"] =
        std::to_string(row.get_int("requires_user_confirmation").value_or(1));
    actions.push_back(action);
  }

  return actions;
}

bool Store::update_recovery_action_stats(int64_t action_id, bool success,
                                          int64_t execution_time_ms) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot update recovery action stats: no database connection");
    }
    return false;
  }

  int64_t now = now_timestamp();

  std::string sql =
      "UPDATE brain_recovery_actions "
      "SET attempted_count = attempted_count + 1, "
      "success_count = success_count + ?, "
      "failure_count = failure_count + ?, "
      "avg_execution_time_ms = (avg_execution_time_ms * attempted_count + ?) / "
      "(attempted_count + 1), "
      "confidence_score = CAST(success_count + ? AS REAL) / (attempted_count + 1), "
      "last_attempted = ? "
      "WHERE id = ?;";

  bool ok = conn_->execute(
      sql, {success ? "1" : "0", success ? "0" : "1",
            std::to_string(execution_time_ms), success ? "1" : "0",
            std::to_string(now), std::to_string(action_id)});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to update recovery action stats: " + conn_->last_error());
  }

  return ok;
}

// ===== Recovery History =====

int64_t Store::begin_recovery_execution(int64_t project_id, int64_t failure_id,
                                         int64_t action_id,
                                         const std::string &execution_mode) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot begin recovery execution: no database connection");
    }
    return 0;
  }

  int64_t now = now_timestamp();
  std::string action_str = action_id > 0 ? std::to_string(action_id) : "";

  std::string sql =
      "INSERT INTO brain_recovery_history "
      "(project_id, failure_id, action_id, started_at, execution_mode) "
      "VALUES (?, ?, ?, ?, ?);";

  bool ok = conn_->execute(
      sql, {std::to_string(project_id), std::to_string(failure_id), action_str,
            std::to_string(now), execution_mode});

  if (!ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to begin recovery execution: " + conn_->last_error());
    }
    return 0;
  }

  return conn_->last_insert_id();
}

bool Store::complete_recovery_execution(int64_t history_id, bool success,
                                         const std::string &output_log,
                                         bool verification_passed,
                                         int64_t execution_time_ms) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot complete recovery execution: no database connection");
    }
    return false;
  }

  int64_t now = now_timestamp();

  std::string sql =
      "UPDATE brain_recovery_history "
      "SET completed_at = ?, success = ?, output_log = ?, "
      "verification_passed = ?, execution_time_ms = ? "
      "WHERE id = ?;";

  bool ok = conn_->execute(
      sql, {std::to_string(now), success ? "1" : "0", output_log,
            verification_passed ? "1" : "0",
            std::to_string(execution_time_ms), std::to_string(history_id)});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to complete recovery execution: " + conn_->last_error());
  }

  return ok;
}

std::vector<std::map<std::string, std::string>>
Store::get_recovery_history(int64_t failure_id) {
  std::vector<std::map<std::string, std::string>> history;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot get recovery history: no database connection");
    }
    return history;
  }

  auto result = conn_->query(
      "SELECT id, project_id, failure_id, action_id, started_at, completed_at, "
      "success, execution_time_ms, execution_mode, verification_passed "
      "FROM brain_recovery_history "
      "WHERE failure_id = ? "
      "ORDER BY started_at DESC;",
      {std::to_string(failure_id)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to get recovery history: " + conn_->last_error());
    }
    return history;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> record;
    record["id"] = std::to_string(row.get_int("id").value_or(0));
    record["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    record["failure_id"] = std::to_string(row.get_int("failure_id").value_or(0));
    record["action_id"] = std::to_string(row.get_int("action_id").value_or(0));
    record["started_at"] = std::to_string(row.get_int("started_at").value_or(0));
    record["completed_at"] =
        std::to_string(row.get_int("completed_at").value_or(0));
    record["success"] = std::to_string(row.get_int("success").value_or(0));
    record["execution_time_ms"] =
        std::to_string(row.get_int("execution_time_ms").value_or(0));
    record["execution_mode"] = row.get("execution_mode").value_or("");
    record["verification_passed"] =
        std::to_string(row.get_int("verification_passed").value_or(0));
    history.push_back(record);
  }

  return history;
}

std::vector<std::map<std::string, std::string>>
Store::get_recent_recoveries(int64_t project_id, int limit) {
  std::vector<std::map<std::string, std::string>> history;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot get recent recoveries: no database connection");
    }
    return history;
  }

  auto result = conn_->query(
      "SELECT id, project_id, failure_id, action_id, started_at, completed_at, "
      "success, execution_time_ms, execution_mode, verification_passed "
      "FROM brain_recovery_history "
      "WHERE project_id = ? "
      "ORDER BY started_at DESC LIMIT ?;",
      {std::to_string(project_id), std::to_string(limit)});

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to get recent recoveries: " + conn_->last_error());
    }
    return history;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> record;
    record["id"] = std::to_string(row.get_int("id").value_or(0));
    record["project_id"] = std::to_string(row.get_int("project_id").value_or(0));
    record["failure_id"] = std::to_string(row.get_int("failure_id").value_or(0));
    record["action_id"] = std::to_string(row.get_int("action_id").value_or(0));
    record["started_at"] = std::to_string(row.get_int("started_at").value_or(0));
    record["completed_at"] =
        std::to_string(row.get_int("completed_at").value_or(0));
    record["success"] = std::to_string(row.get_int("success").value_or(0));
    record["execution_time_ms"] =
        std::to_string(row.get_int("execution_time_ms").value_or(0));
    record["execution_mode"] = row.get("execution_mode").value_or("");
    record["verification_passed"] =
        std::to_string(row.get_int("verification_passed").value_or(0));
    history.push_back(record);
  }

  return history;
}

// ===== Failure Relationships =====

bool Store::add_failure_relationship(int64_t source_failure_id,
                                      int64_t related_failure_id,
                                      const std::string &relationship_type,
                                      double similarity_score) {
  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot add failure relationship: no database connection");
    }
    return false;
  }

  int64_t now = now_timestamp();

  std::string sql =
      "INSERT INTO brain_failure_relationships "
      "(source_failure_id, related_failure_id, relationship_type, "
      "similarity_score, created_at) "
      "VALUES (?, ?, ?, ?, ?);";

  bool ok = conn_->execute(sql, {std::to_string(source_failure_id),
                                  std::to_string(related_failure_id),
                                  relationship_type,
                                  std::to_string(similarity_score),
                                  std::to_string(now)});

  if (!ok && log_) {
    log_->error("Nexus",
                "Failed to add failure relationship: " + conn_->last_error());
  }

  return ok;
}

std::vector<std::map<std::string, std::string>>
Store::get_related_failures(int64_t failure_id,
                            const std::string &relationship_type) {
  std::vector<std::map<std::string, std::string>> relationships;

  if (!conn_) {
    if (log_) {
      log_->error("Nexus", "Cannot get related failures: no database connection");
    }
    return relationships;
  }

  std::string sql =
      "SELECT id, source_failure_id, related_failure_id, relationship_type, "
      "similarity_score, created_at "
      "FROM brain_failure_relationships "
      "WHERE source_failure_id = ? ";

  std::vector<std::string> params = {std::to_string(failure_id)};

  if (!relationship_type.empty()) {
    sql += "AND relationship_type = ? ";
    params.push_back(relationship_type);
  }

  sql += "ORDER BY similarity_score DESC;";

  auto result = conn_->query(sql, params);

  if (!result.ok) {
    if (log_) {
      log_->error("Nexus",
                  "Failed to get related failures: " + conn_->last_error());
    }
    return relationships;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> rel;
    rel["id"] = std::to_string(row.get_int("id").value_or(0));
    rel["source_failure_id"] =
        std::to_string(row.get_int("source_failure_id").value_or(0));
    rel["related_failure_id"] =
        std::to_string(row.get_int("related_failure_id").value_or(0));
    rel["relationship_type"] = row.get("relationship_type").value_or("");
    rel["similarity_score"] = row.get("similarity_score").value_or("0.0");
    rel["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
    relationships.push_back(rel);
  }

  return relationships;
}

// ===== Docker Monitoring: Servers =====

int64_t Store::add_server(const std::string &label, const std::string &host,
                          const std::string &ssh_config) {
  if (!conn_) {
    log_error("Cannot add server: no database connection");
    return 0;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO servers (label, host, agent_status, ssh_config, created_at, updated_at) "
      "VALUES (?, ?, 'unknown', ?, ?, ?);";

  bool ok = conn_->execute(sql, {label, host, ssh_config, std::to_string(now),
                                 std::to_string(now)});

  if (!ok) {
    log_error("Failed to add server '" + label + "': " + conn_->last_error());
    return 0;
  }

  int64_t id = conn_->last_insert_id();
  log_info("Added server '" + label + "' with id " + std::to_string(id));
  return id;
}

std::optional<int64_t> Store::get_server_id(const std::string &label) {
  if (!conn_) {
    log_error("Cannot get server id: no database connection");
    return std::nullopt;
  }

  auto result = conn_->query("SELECT id FROM servers WHERE label = ? LIMIT 1;",
                             {label});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  auto value = result.rows[0].get_int("id");
  if (!value)
    return std::nullopt;
  return *value;
}

std::optional<std::map<std::string, std::string>>
Store::get_server(const std::string &label) {
  if (!conn_) {
    log_error("Cannot get server: no database connection");
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, label, host, agent_version, agent_status, last_heartbeat, "
      "capabilities, ssh_config, agent_container_strategy, "
      "agent_container_local_tar, agent_container_remote_tar, agent_container_image, "
      "created_at, updated_at "
      "FROM servers WHERE label = ? LIMIT 1;",
      {label});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> server;
  server["id"] = std::to_string(row.get_int("id").value_or(0));
  server["label"] = row.get("label").value_or("");
  server["host"] = row.get("host").value_or("");
  server["agent_version"] = row.get("agent_version").value_or("");
  server["agent_status"] = row.get("agent_status").value_or("unknown");
  server["last_heartbeat"] =
      std::to_string(row.get_int("last_heartbeat").value_or(0));
  server["capabilities"] = row.get("capabilities").value_or("");
  server["ssh_config"] = row.get("ssh_config").value_or("{}");
  server["agent_container_strategy"] =
      row.get("agent_container_strategy").value_or("binary");
  server["agent_container_local_tar"] =
      row.get("agent_container_local_tar").value_or("");
  server["agent_container_remote_tar"] =
      row.get("agent_container_remote_tar").value_or("");
  server["agent_container_image"] =
      row.get("agent_container_image").value_or("");
  server["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
  server["updated_at"] = std::to_string(row.get_int("updated_at").value_or(0));
  return server;
}

std::optional<std::map<std::string, std::string>>
Store::get_server_by_id(int64_t server_id) {
  if (!conn_) {
    log_error("Cannot get server by id: no database connection");
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, label, host, agent_version, agent_status, last_heartbeat, "
      "capabilities, ssh_config, agent_container_strategy, "
      "agent_container_local_tar, agent_container_remote_tar, agent_container_image, "
      "created_at, updated_at "
      "FROM servers WHERE id = ? LIMIT 1;",
      {std::to_string(server_id)});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> server;
  server["id"] = std::to_string(row.get_int("id").value_or(0));
  server["label"] = row.get("label").value_or("");
  server["host"] = row.get("host").value_or("");
  server["agent_version"] = row.get("agent_version").value_or("");
  server["agent_status"] = row.get("agent_status").value_or("unknown");
  server["last_heartbeat"] =
      std::to_string(row.get_int("last_heartbeat").value_or(0));
  server["capabilities"] = row.get("capabilities").value_or("");
  server["ssh_config"] = row.get("ssh_config").value_or("{}");
  server["agent_container_strategy"] =
      row.get("agent_container_strategy").value_or("binary");
  server["agent_container_local_tar"] =
      row.get("agent_container_local_tar").value_or("");
  server["agent_container_remote_tar"] =
      row.get("agent_container_remote_tar").value_or("");
  server["agent_container_image"] =
      row.get("agent_container_image").value_or("");
  server["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
  server["updated_at"] = std::to_string(row.get_int("updated_at").value_or(0));
  return server;
}

std::vector<std::map<std::string, std::string>> Store::list_servers() {
  std::vector<std::map<std::string, std::string>> servers;

  if (!conn_) {
    log_error("Cannot list servers: no database connection");
    return servers;
  }

  auto result = conn_->query(
      "SELECT id, label, host, agent_version, agent_status, last_heartbeat, "
      "capabilities, ssh_config, agent_container_strategy, "
      "agent_container_local_tar, agent_container_remote_tar, agent_container_image, "
      "created_at, updated_at "
      "FROM servers ORDER BY label;",
      {});

  if (!result.ok) {
    log_error("Failed to list servers: " + conn_->last_error());
    return servers;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> server;
    server["id"] = std::to_string(row.get_int("id").value_or(0));
    server["label"] = row.get("label").value_or("");
    server["host"] = row.get("host").value_or("");
    server["agent_version"] = row.get("agent_version").value_or("");
    server["agent_status"] = row.get("agent_status").value_or("unknown");
    server["last_heartbeat"] =
        std::to_string(row.get_int("last_heartbeat").value_or(0));
    server["capabilities"] = row.get("capabilities").value_or("");
    server["ssh_config"] = row.get("ssh_config").value_or("{}");
    server["agent_container_strategy"] =
        row.get("agent_container_strategy").value_or("binary");
    server["agent_container_local_tar"] =
        row.get("agent_container_local_tar").value_or("");
    server["agent_container_remote_tar"] =
        row.get("agent_container_remote_tar").value_or("");
    server["agent_container_image"] =
        row.get("agent_container_image").value_or("");
    server["created_at"] =
        std::to_string(row.get_int("created_at").value_or(0));
    server["updated_at"] =
        std::to_string(row.get_int("updated_at").value_or(0));
    servers.push_back(std::move(server));
  }

  return servers;
}

bool Store::update_server_agent_container(int64_t server_id,
                                          const std::string &strategy,
                                          const std::string &local_tar,
                                          const std::string &remote_tar,
                                          const std::string &image) {
  if (!conn_) {
    log_error("Cannot update server container info: no database connection");
    return false;
  }

  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "UPDATE servers SET agent_container_strategy = ?, agent_container_local_tar = ?, "
      "agent_container_remote_tar = ?, agent_container_image = ?, updated_at = ? WHERE id = ?;",
      {strategy, local_tar, remote_tar, image, std::to_string(now),
       std::to_string(server_id)});

  if (!ok) {
    log_error("Failed to update server container info for id " +
              std::to_string(server_id) + ": " + conn_->last_error());
  }

  return ok;
}

bool Store::update_server_heartbeat(int64_t server_id,
                                    const std::string &agent_version,
                                    const std::string &agent_status,
                                    const std::string &capabilities) {
  if (!conn_) {
    log_error("Cannot update server heartbeat: no database connection");
    return false;
  }

  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "UPDATE servers SET agent_version = ?, agent_status = ?, capabilities = ?, "
      "last_heartbeat = ?, updated_at = ? WHERE id = ?;",
      {agent_version, agent_status, capabilities, std::to_string(now),
       std::to_string(now), std::to_string(server_id)});

  if (!ok) {
    log_error("Failed to update server heartbeat for id " +
              std::to_string(server_id) + ": " + conn_->last_error());
  }

  return ok;
}

bool Store::update_server_status(int64_t server_id,
                                 const std::string &agent_status) {
  if (!conn_) {
    log_error("Cannot update server status: no database connection");
    return false;
  }

  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "UPDATE servers SET agent_status = ?, updated_at = ? WHERE id = ?;",
      {agent_status, std::to_string(now), std::to_string(server_id)});

  if (!ok) {
    log_error("Failed to update server status for id " +
              std::to_string(server_id) + ": " + conn_->last_error());
  }

  return ok;
}

bool Store::delete_server(int64_t server_id) {
  if (!conn_) {
    log_error("Cannot delete server: no database connection");
    return false;
  }

  bool ok = conn_->execute("DELETE FROM servers WHERE id = ?;",
                           {std::to_string(server_id)});

  if (!ok) {
    log_error("Failed to delete server id " + std::to_string(server_id) +
              ": " + conn_->last_error());
  }

  return ok;
}

bool Store::update_server_connection(int64_t server_id,
                                     const std::string &host,
                                     const std::string &ssh_config) {
  if (!conn_) {
    log_error("Cannot update server: no database connection");
    return false;
  }

  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "UPDATE servers SET host = ?, ssh_config = ?, updated_at = ? WHERE id = ?;",
      {host, ssh_config, std::to_string(now), std::to_string(server_id)});

  if (!ok) {
    log_error("Failed to update server connection for id " +
              std::to_string(server_id) + ": " + conn_->last_error());
  }

  return ok;
}

// ===== Docker Monitoring: Containers =====

int64_t Store::upsert_container(int64_t server_id,
                                const std::string &container_id,
                                const std::string &name,
                                const std::string &image,
                                const std::string &state,
                                const std::string &status,
                                int64_t created,
                                const std::string &ports_json,
                                const std::string &volumes_json,
                                const std::string &networks_json,
                                const std::string &service_name,
                                const std::string &depends_on_json,
                                const std::string &labels_json,
                                const std::string &health_status,
                                const std::string &restart_policy) {
  if (!conn_) {
    log_error("Cannot upsert container: no database connection");
    return 0;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO containers (server_id, container_id, name, image, state, status, "
      "created, ports, volumes, networks, compose_file_id, service_name, depends_on, "
      "labels, env_vars, health_status, restart_policy, last_seen, created_at, updated_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(server_id, container_id) DO UPDATE SET "
      "name = excluded.name, image = excluded.image, state = excluded.state, "
      "status = excluded.status, created = excluded.created, ports = excluded.ports, "
      "volumes = excluded.volumes, networks = excluded.networks, "
      "service_name = excluded.service_name, depends_on = excluded.depends_on, "
      "labels = excluded.labels, health_status = excluded.health_status, "
      "restart_policy = excluded.restart_policy, last_seen = excluded.last_seen, "
      "updated_at = excluded.updated_at;";

  bool ok = conn_->execute(
      sql,
      {std::to_string(server_id),
       container_id,
       name,
       image,
       state,
       status,
       created ? std::to_string(created) : std::string(),
       ports_json,
       volumes_json,
       networks_json,
       std::string(), // compose_file_id (unknown)
       service_name,
       depends_on_json,
       labels_json,
       std::string(), // env_vars (not captured)
       health_status,
       restart_policy,
       std::to_string(now),
       std::to_string(now),
       std::to_string(now)});

  if (!ok) {
    log_error("Failed to upsert container '" + name + "' (" + container_id +
              ") on server " + std::to_string(server_id) + ": " +
              conn_->last_error());
    return 0;
  }

  return conn_->last_insert_id();
}

std::vector<std::map<std::string, std::string>>
Store::list_containers(int64_t server_id) {
  std::vector<std::map<std::string, std::string>> containers;

  if (!conn_) {
    log_error("Cannot list containers: no database connection");
    return containers;
  }

  std::string sql =
      "SELECT id, server_id, container_id, name, image, state, status, created, "
      "ports, volumes, networks, compose_file_id, service_name, depends_on, labels, "
      "env_vars, health_status, restart_policy, last_seen, created_at, updated_at "
      "FROM containers";

  std::vector<std::string> params;
  if (server_id > 0) {
    sql += " WHERE server_id = ?";
    params.push_back(std::to_string(server_id));
  }

  sql += " ORDER BY name;";

  auto result = conn_->query(sql, params);
  if (!result.ok) {
    log_error("Failed to list containers: " + conn_->last_error());
    return containers;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> cont;
    cont["id"] = std::to_string(row.get_int("id").value_or(0));
    cont["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
    cont["container_id"] = row.get("container_id").value_or("");
    cont["name"] = row.get("name").value_or("");
    cont["image"] = row.get("image").value_or("");
    cont["state"] = row.get("state").value_or("");
    cont["status"] = row.get("status").value_or("");
    cont["created"] = std::to_string(row.get_int("created").value_or(0));
    cont["ports"] = row.get("ports").value_or("");
    cont["volumes"] = row.get("volumes").value_or("");
    cont["networks"] = row.get("networks").value_or("");
    cont["compose_file_id"] =
        std::to_string(row.get_int("compose_file_id").value_or(0));
    cont["service_name"] = row.get("service_name").value_or("");
    cont["depends_on"] = row.get("depends_on").value_or("");
    cont["labels"] = row.get("labels").value_or("");
    cont["env_vars"] = row.get("env_vars").value_or("");
    cont["health_status"] = row.get("health_status").value_or("");
    cont["restart_policy"] = row.get("restart_policy").value_or("");
    cont["last_seen"] = std::to_string(row.get_int("last_seen").value_or(0));
    cont["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
    cont["updated_at"] = std::to_string(row.get_int("updated_at").value_or(0));
    containers.push_back(std::move(cont));
  }

  return containers;
}

std::optional<std::map<std::string, std::string>>
Store::get_container(int64_t server_id, const std::string &container_name) {
  if (!conn_) {
    log_error("Cannot get container: no database connection");
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, server_id, container_id, name, image, state, status, created, "
      "ports, volumes, networks, compose_file_id, service_name, depends_on, labels, "
      "env_vars, health_status, restart_policy, last_seen, created_at, updated_at "
      "FROM containers WHERE server_id = ? AND name = ? LIMIT 1;",
      {std::to_string(server_id), container_name});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> cont;
  cont["id"] = std::to_string(row.get_int("id").value_or(0));
  cont["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
  cont["container_id"] = row.get("container_id").value_or("");
  cont["name"] = row.get("name").value_or("");
  cont["image"] = row.get("image").value_or("");
  cont["state"] = row.get("state").value_or("");
  cont["status"] = row.get("status").value_or("");
  cont["created"] = std::to_string(row.get_int("created").value_or(0));
  cont["ports"] = row.get("ports").value_or("");
  cont["volumes"] = row.get("volumes").value_or("");
  cont["networks"] = row.get("networks").value_or("");
  cont["compose_file_id"] =
      std::to_string(row.get_int("compose_file_id").value_or(0));
  cont["service_name"] = row.get("service_name").value_or("");
  cont["depends_on"] = row.get("depends_on").value_or("");
  cont["labels"] = row.get("labels").value_or("");
  cont["env_vars"] = row.get("env_vars").value_or("");
  cont["health_status"] = row.get("health_status").value_or("");
  cont["restart_policy"] = row.get("restart_policy").value_or("");
  cont["last_seen"] = std::to_string(row.get_int("last_seen").value_or(0));
  cont["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
  cont["updated_at"] = std::to_string(row.get_int("updated_at").value_or(0));
  return cont;
}

std::optional<std::map<std::string, std::string>>
Store::get_container_by_id(int64_t server_id, const std::string &container_id) {
  if (!conn_) {
    log_error("Cannot get container by id: no database connection");
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, server_id, container_id, name, image, state, status, created, "
      "ports, volumes, networks, compose_file_id, service_name, depends_on, labels, "
      "env_vars, health_status, restart_policy, last_seen, created_at, updated_at "
      "FROM containers WHERE server_id = ? AND container_id = ? LIMIT 1;",
      {std::to_string(server_id), container_id});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> cont;
  cont["id"] = std::to_string(row.get_int("id").value_or(0));
  cont["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
  cont["container_id"] = row.get("container_id").value_or("");
  cont["name"] = row.get("name").value_or("");
  cont["image"] = row.get("image").value_or("");
  cont["state"] = row.get("state").value_or("");
  cont["status"] = row.get("status").value_or("");
  cont["created"] = std::to_string(row.get_int("created").value_or(0));
  cont["ports"] = row.get("ports").value_or("");
  cont["volumes"] = row.get("volumes").value_or("");
  cont["networks"] = row.get("networks").value_or("");
  cont["compose_file_id"] =
      std::to_string(row.get_int("compose_file_id").value_or(0));
  cont["service_name"] = row.get("service_name").value_or("");
  cont["depends_on"] = row.get("depends_on").value_or("");
  cont["labels"] = row.get("labels").value_or("");
  cont["env_vars"] = row.get("env_vars").value_or("");
  cont["health_status"] = row.get("health_status").value_or("");
  cont["restart_policy"] = row.get("restart_policy").value_or("");
  cont["last_seen"] = std::to_string(row.get_int("last_seen").value_or(0));
  cont["created_at"] = std::to_string(row.get_int("created_at").value_or(0));
  cont["updated_at"] = std::to_string(row.get_int("updated_at").value_or(0));
  return cont;
}

bool Store::mark_containers_stale(int64_t server_id,
                                   int64_t before_timestamp) {
  if (!conn_) {
    log_error("Cannot mark containers stale: no database connection");
    return false;
  }

  bool ok = conn_->execute(
      "UPDATE containers SET last_seen = ?, updated_at = ? WHERE server_id = ?;",
      {std::to_string(before_timestamp), std::to_string(before_timestamp),
       std::to_string(server_id)});

  if (!ok) {
    log_error("Failed to mark containers stale for server " +
              std::to_string(server_id) + ": " + conn_->last_error());
  }

  return ok;
}

bool Store::delete_stale_containers(int64_t server_id,
                                     int64_t before_timestamp) {
  if (!conn_) {
    log_error("Cannot delete stale containers: no database connection");
    return false;
  }

  bool ok = conn_->execute(
      "DELETE FROM containers WHERE server_id = ? AND last_seen <= ?;",
      {std::to_string(server_id), std::to_string(before_timestamp)});

  if (!ok) {
    log_error("Failed to delete stale containers for server " +
              std::to_string(server_id) + ": " + conn_->last_error());
  }

  return ok;
}

// ===== Docker Monitoring: Compose Files =====

int64_t Store::upsert_compose_file(int64_t server_id,
                                   const std::string &file_path,
                                   const std::string &project_name,
                                   const std::string &services_json,
                                   const std::string &networks_json,
                                   const std::string &volumes_json,
                                   const std::string &file_hash) {
  if (!conn_) {
    log_error("Cannot upsert compose file: no database connection");
    return 0;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO compose_files (server_id, file_path, project_name, services, "
      "networks, volumes, file_hash, last_scan, created_at, updated_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(server_id, file_path) DO UPDATE SET "
      "project_name = excluded.project_name, services = excluded.services, "
      "networks = excluded.networks, volumes = excluded.volumes, "
      "file_hash = excluded.file_hash, last_scan = excluded.last_scan, "
      "updated_at = excluded.updated_at;";

  bool ok = conn_->execute(
      sql,
      {std::to_string(server_id),
       file_path,
       project_name,
       services_json,
       networks_json,
       volumes_json,
       file_hash,
       std::to_string(now),
       std::to_string(now),
       std::to_string(now)});

  if (!ok) {
    log_error("Failed to upsert compose file '" + file_path +
              "' for server " + std::to_string(server_id) + ": " +
              conn_->last_error());
    return 0;
  }

  return conn_->last_insert_id();
}

std::vector<std::map<std::string, std::string>>
Store::list_compose_files(int64_t server_id) {
  std::vector<std::map<std::string, std::string>> files;

  if (!conn_) {
    log_error("Cannot list compose files: no database connection");
    return files;
  }

  std::string sql =
      "SELECT id, server_id, file_path, project_name, services, networks, volumes, "
      "file_hash, last_scan, created_at, updated_at FROM compose_files";

  std::vector<std::string> params;
  if (server_id > 0) {
    sql += " WHERE server_id = ?";
    params.push_back(std::to_string(server_id));
  }
  sql += " ORDER BY file_path;";

  auto result = conn_->query(sql, params);
  if (!result.ok) {
    log_error("Failed to list compose files: " + conn_->last_error());
    return files;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> entry;
    entry["id"] = std::to_string(row.get_int("id").value_or(0));
    entry["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
    entry["file_path"] = row.get("file_path").value_or("");
    entry["project_name"] = row.get("project_name").value_or("");
    entry["services"] = row.get("services").value_or("");
    entry["networks"] = row.get("networks").value_or("");
    entry["volumes"] = row.get("volumes").value_or("");
    entry["file_hash"] = row.get("file_hash").value_or("");
    entry["last_scan"] = std::to_string(row.get_int("last_scan").value_or(0));
    entry["created_at"] =
        std::to_string(row.get_int("created_at").value_or(0));
    entry["updated_at"] =
        std::to_string(row.get_int("updated_at").value_or(0));
    files.push_back(std::move(entry));
  }

  return files;
}

std::optional<std::map<std::string, std::string>>
Store::get_compose_file(int64_t server_id, const std::string &file_path) {
  if (!conn_) {
    log_error("Cannot get compose file: no database connection");
    return std::nullopt;
  }

  auto result = conn_->query(
      "SELECT id, server_id, file_path, project_name, services, networks, volumes, "
      "file_hash, last_scan, created_at, updated_at FROM compose_files "
      "WHERE server_id = ? AND file_path = ? LIMIT 1;",
      {std::to_string(server_id), file_path});

  if (!result.ok || result.rows.empty()) {
    return std::nullopt;
  }

  const auto &row = result.rows[0];
  std::map<std::string, std::string> entry;
  entry["id"] = std::to_string(row.get_int("id").value_or(0));
  entry["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
  entry["file_path"] = row.get("file_path").value_or("");
  entry["project_name"] = row.get("project_name").value_or("");
  entry["services"] = row.get("services").value_or("");
  entry["networks"] = row.get("networks").value_or("");
  entry["volumes"] = row.get("volumes").value_or("");
  entry["file_hash"] = row.get("file_hash").value_or("");
  entry["last_scan"] =
      std::to_string(row.get_int("last_scan").value_or(0));
  entry["created_at"] =
      std::to_string(row.get_int("created_at").value_or(0));
  entry["updated_at"] =
      std::to_string(row.get_int("updated_at").value_or(0));
  return entry;
}

// ===== Docker Monitoring: Images =====

int64_t Store::upsert_docker_image(int64_t server_id,
                                   const std::string &image_id,
                                   const std::string &repository,
                                   const std::string &tag,
                                   int64_t size_bytes,
                                   int64_t created) {
  if (!conn_) {
    log_error("Cannot upsert docker image: no database connection");
    return 0;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO docker_images (server_id, image_id, repository, tag, size_bytes, "
      "created, last_seen, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(server_id, image_id) DO UPDATE SET "
      "repository = excluded.repository, tag = excluded.tag, "
      "size_bytes = excluded.size_bytes, created = excluded.created, "
      "last_seen = excluded.last_seen, updated_at = excluded.updated_at;";

  bool ok = conn_->execute(
      sql,
      {std::to_string(server_id),
       image_id,
       repository,
       tag,
       size_bytes ? std::to_string(size_bytes) : std::string(),
       created ? std::to_string(created) : std::string(),
       std::to_string(now),
       std::to_string(now),
       std::to_string(now)});

  if (!ok) {
    log_error("Failed to upsert docker image '" + image_id +
              "' for server " + std::to_string(server_id) + ": " +
              conn_->last_error());
    return 0;
  }

  return conn_->last_insert_id();
}

std::vector<std::map<std::string, std::string>>
Store::list_docker_images(int64_t server_id) {
  std::vector<std::map<std::string, std::string>> images;

  if (!conn_) {
    log_error("Cannot list docker images: no database connection");
    return images;
  }

  std::string sql =
      "SELECT id, server_id, image_id, repository, tag, size_bytes, created, last_seen, "
      "created_at, updated_at FROM docker_images";

  std::vector<std::string> params;
  if (server_id > 0) {
    sql += " WHERE server_id = ?";
    params.push_back(std::to_string(server_id));
  }
  sql += " ORDER BY repository, tag;";

  auto result = conn_->query(sql, params);
  if (!result.ok) {
    log_error("Failed to list docker images: " + conn_->last_error());
    return images;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> img;
    img["id"] = std::to_string(row.get_int("id").value_or(0));
    img["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
    img["image_id"] = row.get("image_id").value_or("");
    img["repository"] = row.get("repository").value_or("");
    img["tag"] = row.get("tag").value_or("");
    img["size_bytes"] =
        std::to_string(row.get_int("size_bytes").value_or(0));
    img["created"] = std::to_string(row.get_int("created").value_or(0));
    img["last_seen"] = std::to_string(row.get_int("last_seen").value_or(0));
    img["created_at"] =
        std::to_string(row.get_int("created_at").value_or(0));
    img["updated_at"] =
        std::to_string(row.get_int("updated_at").value_or(0));
    images.push_back(std::move(img));
  }

  return images;
}

// ===== Docker Monitoring: Networks =====

int64_t Store::upsert_docker_network(int64_t server_id,
                                     const std::string &network_id,
                                     const std::string &name,
                                     const std::string &driver,
                                     const std::string &scope,
                                     const std::string &ipam_config_json) {
  if (!conn_) {
    log_error("Cannot upsert docker network: no database connection");
    return 0;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO docker_networks (server_id, network_id, name, driver, scope, "
      "ipam_config, last_seen, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(server_id, network_id) DO UPDATE SET "
      "name = excluded.name, driver = excluded.driver, scope = excluded.scope, "
      "ipam_config = excluded.ipam_config, last_seen = excluded.last_seen, "
      "updated_at = excluded.updated_at;";

  bool ok = conn_->execute(
      sql,
      {std::to_string(server_id),
       network_id,
       name,
       driver,
       scope,
       ipam_config_json,
       std::to_string(now),
       std::to_string(now),
       std::to_string(now)});

  if (!ok) {
    log_error("Failed to upsert docker network '" + name +
              "' for server " + std::to_string(server_id) + ": " +
              conn_->last_error());
    return 0;
  }

  return conn_->last_insert_id();
}

std::vector<std::map<std::string, std::string>>
Store::list_docker_networks(int64_t server_id) {
  std::vector<std::map<std::string, std::string>> networks;

  if (!conn_) {
    log_error("Cannot list docker networks: no database connection");
    return networks;
  }

  std::string sql =
      "SELECT id, server_id, network_id, name, driver, scope, ipam_config, last_seen, "
      "created_at, updated_at FROM docker_networks";

  std::vector<std::string> params;
  if (server_id > 0) {
    sql += " WHERE server_id = ?";
    params.push_back(std::to_string(server_id));
  }
  sql += " ORDER BY name;";

  auto result = conn_->query(sql, params);
  if (!result.ok) {
    log_error("Failed to list docker networks: " + conn_->last_error());
    return networks;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> net;
    net["id"] = std::to_string(row.get_int("id").value_or(0));
    net["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
    net["network_id"] = row.get("network_id").value_or("");
    net["name"] = row.get("name").value_or("");
    net["driver"] = row.get("driver").value_or("");
    net["scope"] = row.get("scope").value_or("");
    net["ipam_config"] = row.get("ipam_config").value_or("");
    net["last_seen"] = std::to_string(row.get_int("last_seen").value_or(0));
    net["created_at"] =
        std::to_string(row.get_int("created_at").value_or(0));
    net["updated_at"] =
        std::to_string(row.get_int("updated_at").value_or(0));
    networks.push_back(std::move(net));
  }

  return networks;
}

// ===== Docker Monitoring: Volumes =====

int64_t Store::upsert_docker_volume(int64_t server_id,
                                    const std::string &volume_name,
                                    const std::string &driver,
                                    const std::string &mountpoint,
                                    const std::string &labels_json) {
  if (!conn_) {
    log_error("Cannot upsert docker volume: no database connection");
    return 0;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO docker_volumes (server_id, volume_name, driver, mountpoint, labels, "
      "last_seen, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(server_id, volume_name) DO UPDATE SET "
      "driver = excluded.driver, mountpoint = excluded.mountpoint, "
      "labels = excluded.labels, last_seen = excluded.last_seen, "
      "updated_at = excluded.updated_at;";

  bool ok = conn_->execute(
      sql,
      {std::to_string(server_id), volume_name, driver, mountpoint, labels_json,
       std::to_string(now), std::to_string(now), std::to_string(now)});

  if (!ok) {
    log_error("Failed to upsert docker volume '" + volume_name +
              "' for server " + std::to_string(server_id) + ": " +
              conn_->last_error());
    return 0;
  }

  return conn_->last_insert_id();
}

std::vector<std::map<std::string, std::string>>
Store::list_docker_volumes(int64_t server_id) {
  std::vector<std::map<std::string, std::string>> volumes;

  if (!conn_) {
    log_error("Cannot list docker volumes: no database connection");
    return volumes;
  }

  std::string sql =
      "SELECT id, server_id, volume_name, driver, mountpoint, labels, last_seen, "
      "created_at, updated_at FROM docker_volumes";

  std::vector<std::string> params;
  if (server_id > 0) {
    sql += " WHERE server_id = ?";
    params.push_back(std::to_string(server_id));
  }
  sql += " ORDER BY volume_name;";

  auto result = conn_->query(sql, params);
  if (!result.ok) {
    log_error("Failed to list docker volumes: " + conn_->last_error());
    return volumes;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> vol;
    vol["id"] = std::to_string(row.get_int("id").value_or(0));
    vol["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
    vol["volume_name"] = row.get("volume_name").value_or("");
    vol["driver"] = row.get("driver").value_or("");
    vol["mountpoint"] = row.get("mountpoint").value_or("");
    vol["labels"] = row.get("labels").value_or("");
    vol["last_seen"] = std::to_string(row.get_int("last_seen").value_or(0));
    vol["created_at"] =
        std::to_string(row.get_int("created_at").value_or(0));
    vol["updated_at"] =
        std::to_string(row.get_int("updated_at").value_or(0));
    volumes.push_back(std::move(vol));
  }

  return volumes;
}

// ===== Docker Monitoring: Status History =====

int64_t Store::add_docker_status_event(int64_t server_id,
                                       const std::string &container_id,
                                       const std::string &container_name,
                                       const std::string &event_type,
                                       const std::string &old_state,
                                       const std::string &new_state,
                                       const std::string &metadata_json) {
  if (!conn_) {
    log_error("Cannot add docker status event: no database connection");
    return 0;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO docker_status_history (server_id, container_id, container_name, "
      "event_type, old_state, new_state, metadata, timestamp) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

  bool ok = conn_->execute(
      sql,
      {std::to_string(server_id), container_id, container_name, event_type,
       old_state, new_state, metadata_json, std::to_string(now)});

  if (!ok) {
    log_error("Failed to add docker status event for container '" + container_id +
              "' on server " + std::to_string(server_id) + ": " +
              conn_->last_error());
    return 0;
  }

  return conn_->last_insert_id();
}

std::vector<std::map<std::string, std::string>>
Store::get_container_history(int64_t server_id,
                             const std::string &container_name,
                             int limit) {
  std::vector<std::map<std::string, std::string>> events;

  if (!conn_) {
    log_error("Cannot get container history: no database connection");
    return events;
  }

  auto result = conn_->query(
      "SELECT id, server_id, container_id, container_name, event_type, old_state, "
      "new_state, metadata, timestamp FROM docker_status_history "
      "WHERE server_id = ? AND container_name = ? ORDER BY timestamp DESC LIMIT ?;",
      {std::to_string(server_id), container_name, std::to_string(limit)});

  if (!result.ok) {
    log_error("Failed to get container history: " + conn_->last_error());
    return events;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> item;
    item["id"] = std::to_string(row.get_int("id").value_or(0));
    item["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
    item["container_id"] = row.get("container_id").value_or("");
    item["container_name"] = row.get("container_name").value_or("");
    item["event_type"] = row.get("event_type").value_or("");
    item["old_state"] = row.get("old_state").value_or("");
    item["new_state"] = row.get("new_state").value_or("");
    item["metadata"] = row.get("metadata").value_or("");
    item["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
    events.push_back(std::move(item));
  }

  return events;
}

std::vector<std::map<std::string, std::string>>
Store::get_recent_docker_events(int64_t server_id, int limit) {
  std::vector<std::map<std::string, std::string>> events;

  if (!conn_) {
    log_error("Cannot get recent docker events: no database connection");
    return events;
  }

  std::string sql =
      "SELECT id, server_id, container_id, container_name, event_type, old_state, "
      "new_state, metadata, timestamp FROM docker_status_history";

  std::vector<std::string> params;
  if (server_id > 0) {
    sql += " WHERE server_id = ?";
    params.push_back(std::to_string(server_id));
  }
  sql += " ORDER BY timestamp DESC LIMIT ?;";
  params.push_back(std::to_string(limit));

  auto result = conn_->query(sql, params);
  if (!result.ok) {
    log_error("Failed to get recent docker events: " + conn_->last_error());
    return events;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> item;
    item["id"] = std::to_string(row.get_int("id").value_or(0));
    item["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
    item["container_id"] = row.get("container_id").value_or("");
    item["container_name"] = row.get("container_name").value_or("");
    item["event_type"] = row.get("event_type").value_or("");
    item["old_state"] = row.get("old_state").value_or("");
    item["new_state"] = row.get("new_state").value_or("");
    item["metadata"] = row.get("metadata").value_or("");
    item["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
    events.push_back(std::move(item));
  }

  return events;
}

// ===== Docker Monitoring: Agent Updates =====

int64_t Store::log_agent_update(int64_t server_id,
                                const std::string &update_type,
                                const std::string &payload_json) {
  if (!conn_) {
    log_error("Cannot log agent update: no database connection");
    return 0;
  }

  int64_t now = now_timestamp();
  bool ok = conn_->execute(
      "INSERT INTO agent_updates (server_id, update_type, payload_json, timestamp) "
      "VALUES (?, ?, ?, ?);",
      {std::to_string(server_id), update_type, payload_json,
       std::to_string(now)});

  if (!ok) {
    log_error("Failed to log agent update for server " +
              std::to_string(server_id) + ": " + conn_->last_error());
    return 0;
  }

  return conn_->last_insert_id();
}

bool Store::mark_agent_update_processed(int64_t update_id, bool success,
                                        const std::string &error_message) {
  if (!conn_) {
    log_error("Cannot mark agent update processed: no database connection");
    return false;
  }

  std::string processed_value = success ? "1" : "0";
  bool ok = conn_->execute(
      "UPDATE agent_updates SET processed = ?, error_message = ? WHERE id = ?;",
      {processed_value, error_message, std::to_string(update_id)});

  if (!ok) {
    log_error("Failed to mark agent update processed for id " +
              std::to_string(update_id) + ": " + conn_->last_error());
  }

  return ok;
}

std::vector<std::map<std::string, std::string>>
Store::get_unprocessed_agent_updates(int limit) {
  std::vector<std::map<std::string, std::string>> updates;

  if (!conn_) {
    log_error("Cannot get agent updates: no database connection");
    return updates;
  }

  auto result = conn_->query(
      "SELECT id, server_id, update_type, payload_json, processed, error_message, "
      "timestamp FROM agent_updates WHERE processed = 0 ORDER BY timestamp ASC LIMIT ?;",
      {std::to_string(limit)});

  if (!result.ok) {
    log_error("Failed to get agent updates: " + conn_->last_error());
    return updates;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> up;
    up["id"] = std::to_string(row.get_int("id").value_or(0));
    up["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
    up["update_type"] = row.get("update_type").value_or("");
    up["payload_json"] = row.get("payload_json").value_or("");
    up["processed"] = std::to_string(row.get_int("processed").value_or(0));
    up["error_message"] = row.get("error_message").value_or("");
    up["timestamp"] = std::to_string(row.get_int("timestamp").value_or(0));
    updates.push_back(std::move(up));
  }

  return updates;
}

// ===== Docker Monitoring: Commands =====

int64_t Store::add_docker_command(int64_t server_id,
                                  const std::string &container_id,
                                  const std::string &command,
                                  const std::string &params_json,
                                  const std::string &issued_by) {
  if (!conn_) {
    log_error("Cannot add docker command: no database connection");
    return 0;
  }

  int64_t now = now_timestamp();
  std::string sql =
      "INSERT INTO docker_commands (server_id, container_id, command, params, status, "
      "exit_code, output, error, issued_by, issued_at) "
      "VALUES (?, ?, ?, ?, 'pending', NULL, NULL, NULL, ?, ?);";

  bool ok = conn_->execute(sql,
                           {std::to_string(server_id), container_id, command,
                            params_json, issued_by, std::to_string(now)});

  if (!ok) {
    log_error("Failed to add docker command '" + command +
              "' for server " + std::to_string(server_id) + ": " +
              conn_->last_error());
    return 0;
  }

  return conn_->last_insert_id();
}

bool Store::update_docker_command_status(int64_t command_id,
                                         const std::string &status,
                                         int exit_code,
                                         const std::string &output,
                                         const std::string &error) {
  if (!conn_) {
    log_error("Cannot update docker command: no database connection");
    return false;
  }

  int64_t now = now_timestamp();
  std::string completed_at;
  if (status == "success" || status == "failed") {
    completed_at = std::to_string(now);
  }

  std::string sql =
      "UPDATE docker_commands SET status = ?, exit_code = ?, output = ?, error = ?, "
      "completed_at = ? WHERE id = ?;";

  bool ok = conn_->execute(sql,
                           {status,
                            exit_code >= 0 ? std::to_string(exit_code) : std::string(),
                            output,
                            error,
                            completed_at,
                            std::to_string(command_id)});

  if (!ok) {
    log_error("Failed to update docker command id " + std::to_string(command_id) +
              ": " + conn_->last_error());
  }

  return ok;
}

std::vector<std::map<std::string, std::string>>
Store::list_docker_commands(int64_t server_id, int limit) {
  std::vector<std::map<std::string, std::string>> commands;

  if (!conn_) {
    log_error("Cannot list docker commands: no database connection");
    return commands;
  }

  std::string sql =
      "SELECT id, server_id, container_id, command, params, status, exit_code, output, "
      "error, issued_by, issued_at, completed_at FROM docker_commands";

  std::vector<std::string> params;
  if (server_id > 0) {
    sql += " WHERE server_id = ?";
    params.push_back(std::to_string(server_id));
  }
  sql += " ORDER BY issued_at DESC LIMIT ?;";
  params.push_back(std::to_string(limit));

  auto result = conn_->query(sql, params);
  if (!result.ok) {
    log_error("Failed to list docker commands: " + conn_->last_error());
    return commands;
  }

  for (const auto &row : result.rows) {
    std::map<std::string, std::string> cmd;
    cmd["id"] = std::to_string(row.get_int("id").value_or(0));
    cmd["server_id"] = std::to_string(row.get_int("server_id").value_or(0));
    cmd["container_id"] = row.get("container_id").value_or("");
    cmd["command"] = row.get("command").value_or("");
    cmd["params"] = row.get("params").value_or("");
    cmd["status"] = row.get("status").value_or("");
    cmd["exit_code"] = std::to_string(row.get_int("exit_code").value_or(0));
    cmd["output"] = row.get("output").value_or("");
    cmd["error"] = row.get("error").value_or("");
    cmd["issued_by"] = row.get("issued_by").value_or("");
    cmd["issued_at"] = std::to_string(row.get_int("issued_at").value_or(0));
    cmd["completed_at"] =
        std::to_string(row.get_int("completed_at").value_or(0));
    commands.push_back(std::move(cmd));
  }

  return commands;
}

void Store::log_debug(const std::string &msg) const {
  if (log_) {
    log_->debug("Nexus", msg);
  }
}

void Store::log_info(const std::string &msg) const {
  if (log_) {
    log_->info("Nexus", msg);
  }
}

void Store::log_warn(const std::string &msg) const {
  if (log_) {
    log_->warn("Nexus", msg);
  }
}

void Store::log_error(const std::string &msg) const {
  if (log_) {
    log_->error("Nexus", msg);
  }
}

} // namespace nazg::nexus

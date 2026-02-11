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

#include "nexus/migrator.hpp"
#include "blackbox/logger.hpp"
#include "config/parser.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace nazg::nexus {

namespace {
// Get path to current executable
std::string current_exe() {
#if defined(__linux__)
  char buf[4096];
  ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = 0;
    return std::string(buf);
  }
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0)
    return std::string(buf);
#endif
  return {};
}
} // namespace

Migrator::Migrator(Connection *conn, nazg::blackbox::logger *log)
    : conn_(conn), log_(log) {}

int Migrator::current_version() { return get_schema_version(); }

int Migrator::latest_version() {
  if (!loaded_)
    load_migrations();
  if (migrations_.empty())
    return 0;
  return migrations_.back().version;
}

bool Migrator::needs_migration() {
  return current_version() < latest_version();
}

bool Migrator::migrate() { return migrate_to(latest_version()); }

bool Migrator::migrate_to(int target_version) {
  if (!conn_ || !conn_->is_connected()) {
    if (log_)
      log_->error("Nexus", "Cannot migrate: database not connected");
    return false;
  }

  if (!loaded_)
    load_migrations();

  int current = get_schema_version();
  if (current >= target_version) {
    if (log_)
      log_->debug("Nexus", "Database already at version " +
                               std::to_string(current));
    return true;
  }

  if (log_)
    log_->info("Nexus", "Migrating from version " + std::to_string(current) +
                            " to " + std::to_string(target_version));

  for (const auto &mig : migrations_) {
    if (mig.version <= current)
      continue;
    if (mig.version > target_version)
      break;

    if (!apply_migration(mig))
      return false;
    if (log_)
      log_->debug("Nexus", "Migration " + std::to_string(mig.version) +
                                " applied successfully");
  }

  if (log_)
    log_->info("Nexus", "Migration complete: now at version " +
                            std::to_string(get_schema_version()));

  return true;
}

bool Migrator::load_migrations() {
  loaded_ = true;
  migrations_.clear();

  // Find migrations directory relative to this binary or source tree
  std::vector<fs::path> search_paths;

  // 1. Check version-specific migrations (bundled with binary)
  //    e.g., ~/.local/share/nazg/versions/v-local/bin/nazg -> ../migrations/
  auto exe = current_exe();
  if (!exe.empty()) {
    fs::path exe_path = exe;
    fs::path version_migrations = exe_path.parent_path().parent_path() / "migrations";
    search_paths.push_back(version_migrations);
  }

  // 2. Source tree (for development)
  search_paths.push_back(fs::path(__FILE__).parent_path().parent_path() / "migrations");
  search_paths.push_back(fs::current_path() / "modules/nexus/migrations");

  // 3. System/user install locations
  search_paths.push_back(fs::path("/usr/share/nazg/migrations"));
  search_paths.push_back(fs::path(nazg::config::default_data_dir()) / "migrations");

  fs::path migrations_dir;
  for (const auto &p : search_paths) {
    if (fs::exists(p) && fs::is_directory(p)) {
      migrations_dir = p;
      break;
    }
    if (log_)
      log_->debug("Nexus", "Checked migrations path: " + p.string());
  }

  if (migrations_dir.empty()) {
    if (log_)
      log_->warn("Nexus", "No migrations directory found");
    return false;
  }

  if (log_)
    log_->debug("Nexus", "Loading migrations from " + migrations_dir.string());

  // Read all .sql files
  std::regex version_pattern(R"((\d+)_.*\.sql)");
  for (const auto &entry : fs::directory_iterator(migrations_dir)) {
    if (!entry.is_regular_file())
      continue;

    std::string filename = entry.path().filename().string();
    std::smatch match;
    if (!std::regex_match(filename, match, version_pattern))
      continue;

    int version = std::stoi(match[1].str());

    // Read file content
    std::ifstream file(entry.path());
    if (!file) {
      if (log_)
        log_->warn("Nexus", "Failed to read migration: " + filename);
      continue;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    migrations_.push_back({version, buffer.str()});

    if (log_)
      log_->debug("Nexus", "Loaded migration " + std::to_string(version) +
                               ": " + filename);
  }

  // Sort by version
  std::sort(migrations_.begin(), migrations_.end(),
            [](const Migration &a, const Migration &b) {
              return a.version < b.version;
            });

  if (log_)
    log_->info("Nexus", "Loaded " + std::to_string(migrations_.size()) +
                            " migration(s)");

  return !migrations_.empty();
}

bool Migrator::apply_migration(const Migration &mig) {
  if (log_)
    log_->info("Nexus", "Applying migration " + std::to_string(mig.version));

  // Execute the migration SQL (use execute_script for multi-statement support)
  if (!conn_->execute_script(mig.sql)) {
    if (log_)
      log_->error("Nexus", "Migration " + std::to_string(mig.version) +
                               " failed: " + conn_->last_error());
    return false;
  }

  return true;
}

int Migrator::get_schema_version() {
  if (!conn_ || !conn_->is_connected())
    return 0;

  // Check if schema_version table exists
  auto result = conn_->query(
      "SELECT name FROM sqlite_master WHERE type='table' AND "
      "name='schema_version';");

  if (!result.ok || result.empty()) {
    // Table doesn't exist, version is 0
    return 0;
  }

  // Get max version
  result = conn_->query("SELECT MAX(version) as version FROM schema_version;");
  if (!result.ok || result.empty())
    return 0;

  auto ver = result.rows[0].get_int("version");
  return ver.value_or(0);
}

} // namespace nazg::nexus

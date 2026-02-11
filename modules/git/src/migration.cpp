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

#include "git/migration.hpp"
#include "git/server_registry.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace nazg::git {

MigrationPlanner::MigrationPlanner(nazg::nexus::Store* store,
                                   nazg::blackbox::logger* log)
    : store_(store), log_(log) {}

std::vector<LocalRepoInfo>
MigrationPlanner::scan_local_repos(const std::string& source_path) const {
  std::vector<LocalRepoInfo> repos;
  if (!fs::exists(source_path)) {
    if (log_) {
      log_->warn("Git/migrate", "Source path not found: " + source_path);
    }
    return repos;
  }

  for (const auto& entry : fs::directory_iterator(source_path)) {
    if (!entry.is_directory()) {
      continue;
    }
    if (entry.path().filename().string().size() >= 4 &&
        entry.path().filename().string().substr(entry.path().filename().string().size() - 4) == ".git") {
      LocalRepoInfo info;
      info.name = entry.path().filename().string();
      info.path = entry.path().string();
      repos.push_back(std::move(info));
    }
  }

  return repos;
}

int64_t MigrationPlanner::record_migration_start(const std::string& server_label,
                                                 const LocalRepoInfo& repo,
                                                 const std::string& source_path,
                                                 int64_t project_id) {
  if (!store_) {
    return 0;
  }

  auto server = store_->get_git_server(server_label);
  if (!server) {
    if (log_) {
      log_->warn("Git/migrate", "Server not found while recording migration: " + server_label);
    }
    return 0;
  }

  nexus::RepoMigrationRecord rec;
  rec.server_id = server->id;
  rec.project_id = project_id;
  rec.repo_name = repo.name;
  rec.source_path = source_path;
  rec.branch_count = repo.branch_count;
  rec.tag_count = repo.tag_count;
  rec.size_bytes = repo.size_bytes;
  rec.status = "running";
  rec.started_at = std::time(nullptr);
  return store_->add_repo_migration(rec);
}

void MigrationPlanner::record_migration_failure(int64_t migration_id,
                                                const std::string& server_label,
                                                const std::string& error_message) {
  if (!store_ || migration_id == 0) {
    return;
  }
  store_->update_repo_migration_status(migration_id, "failed", error_message);
  if (log_) {
    log_->error("Git/migrate", "Migration " + std::to_string(migration_id) +
                                " failed for " + server_label + ": " + error_message);
  }
}

void MigrationPlanner::record_migration_success(int64_t migration_id,
                                                const std::string& server_label) {
  if (!store_ || migration_id == 0) {
    return;
  }
  store_->mark_repo_migration_completed(migration_id, std::time(nullptr));
  if (log_) {
    log_->info("Git/migrate", "Migration " + std::to_string(migration_id) +
                               " completed for " + server_label);
  }
}

} // namespace nazg::git

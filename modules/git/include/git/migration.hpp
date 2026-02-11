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
#include <string>
#include <vector>
#include <optional>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::git {

struct LocalRepoInfo {
  std::string name;
  std::string path;
  int branch_count = 0;
  int tag_count = 0;
  int64_t size_bytes = 0;
};

class MigrationPlanner {
public:
  MigrationPlanner(nazg::nexus::Store* store,
                   nazg::blackbox::logger* log);

  std::vector<LocalRepoInfo> scan_local_repos(const std::string& source_path) const;
  int64_t record_migration_start(const std::string& server_label,
                                 const LocalRepoInfo& repo,
                                 const std::string& source_path,
                                 int64_t project_id = 0);
  void record_migration_failure(int64_t migration_id,
                                const std::string& server_label,
                                const std::string& error_message);
  void record_migration_success(int64_t migration_id,
                                const std::string& server_label);

private:
  nazg::nexus::Store* store_;
  nazg::blackbox::logger* log_;
};

} // namespace nazg::git

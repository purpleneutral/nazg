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
#include <memory>
#include <string>
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::nexus {

class Migrator {
public:
  explicit Migrator(Connection *conn, nazg::blackbox::logger *log = nullptr);

  // Get current schema version from DB
  int current_version();

  // Get latest available migration version
  int latest_version();

  // Check if migration is needed
  bool needs_migration();

  // Apply all pending migrations
  bool migrate();

  // Apply migrations up to a specific version
  bool migrate_to(int target_version);

private:
  struct Migration {
    int version;
    std::string sql;
  };

  bool load_migrations();
  bool apply_migration(const Migration &mig);
  int get_schema_version();

  Connection *conn_ = nullptr;
  nazg::blackbox::logger *log_ = nullptr;
  std::vector<Migration> migrations_;
  bool loaded_ = false;
};

} // namespace nazg::nexus

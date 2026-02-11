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
#include <sqlite3.h>
#include <string>

namespace nazg::blackbox {
class logger;
}

namespace nazg::nexus {

class SqliteDriver : public Connection {
public:
  explicit SqliteDriver(const std::string &db_path,
                        nazg::blackbox::logger *log = nullptr);
  ~SqliteDriver() override;

  // Disable copy, allow move
  SqliteDriver(const SqliteDriver &) = delete;
  SqliteDriver &operator=(const SqliteDriver &) = delete;
  SqliteDriver(SqliteDriver &&) noexcept;
  SqliteDriver &operator=(SqliteDriver &&) noexcept;

  // Connection interface
  bool execute(const std::string &sql) override;
  bool execute(const std::string &sql,
               const std::vector<std::string> &params) override;
  bool execute_script(const std::string &sql) override;

  QueryResult query(const std::string &sql) override;
  QueryResult query(const std::string &sql,
                    const std::vector<std::string> &params) override;

  int64_t last_insert_id() override;

  bool begin_transaction() override;
  bool commit() override;
  bool rollback() override;

  std::string last_error() const override;
  bool is_connected() const override;

  // SQLite-specific: enable WAL mode
  bool enable_wal_mode();

private:
  bool open_db(const std::string &path);
  void close_db();
  QueryResult exec_query(const std::string &sql,
                         const std::vector<std::string> *params);

  sqlite3 *db_ = nullptr;
  std::string db_path_;
  std::string last_error_;
  nazg::blackbox::logger *log_ = nullptr;
};

} // namespace nazg::nexus

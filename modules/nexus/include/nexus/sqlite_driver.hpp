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

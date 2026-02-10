#include "nexus/sqlite_driver.hpp"
#include "blackbox/logger.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace nazg::nexus {

SqliteDriver::SqliteDriver(const std::string &db_path,
                           nazg::blackbox::logger *log)
    : db_path_(db_path), log_(log) {
  open_db(db_path);
}

SqliteDriver::~SqliteDriver() { close_db(); }

SqliteDriver::SqliteDriver(SqliteDriver &&other) noexcept
    : db_(other.db_), db_path_(std::move(other.db_path_)),
      last_error_(std::move(other.last_error_)), log_(other.log_) {
  other.db_ = nullptr;
}

SqliteDriver &SqliteDriver::operator=(SqliteDriver &&other) noexcept {
  if (this != &other) {
    close_db();
    db_ = other.db_;
    db_path_ = std::move(other.db_path_);
    last_error_ = std::move(other.last_error_);
    log_ = other.log_;
    other.db_ = nullptr;
  }
  return *this;
}

bool SqliteDriver::open_db(const std::string &path) {
  if (log_)
    log_->debug("Nexus", "Opening SQLite database at " + path);
  // Ensure parent directory exists
  try {
    fs::path p(path);
    if (p.has_parent_path()) {
      fs::create_directories(p.parent_path());
    }
  } catch (const std::exception &e) {
    last_error_ = std::string("Failed to create DB directory: ") + e.what();
    if (log_)
      log_->error("Nexus", last_error_);
    return false;
  }

  int rc = sqlite3_open(path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    last_error_ =
        std::string("Failed to open database: ") + sqlite3_errmsg(db_);
    if (log_)
      log_->error("Nexus", last_error_);
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  if (log_)
    log_->info("Nexus", "Database opened: " + path);

  // Enable foreign keys by default
  execute("PRAGMA foreign_keys = ON;");

  return true;
}

void SqliteDriver::close_db() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  if (log_)
    log_->debug("Nexus", "Database closed");
  }
}

bool SqliteDriver::execute(const std::string &sql) {
  return execute(sql, {});
}

bool SqliteDriver::execute(const std::string &sql,
                           const std::vector<std::string> &params) {
  auto start = std::chrono::steady_clock::now();

  if (!db_) {
    last_error_ = "Database not open";
    return false;
  }

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    last_error_ = sqlite3_errmsg(db_);
    if (log_)
      log_->error("Nexus", "Query prepare failed: " + last_error_);
    return false;
  }

  // Bind parameters
  for (size_t i = 0; i < params.size(); ++i) {
    rc = sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
      last_error_ = sqlite3_errmsg(db_);
      sqlite3_finalize(stmt);
      if (log_)
        log_->error("Nexus", "Parameter bind failed: " + last_error_);
      return false;
    }
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  auto end = std::chrono::steady_clock::now();
  auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();

  if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
    last_error_ = sqlite3_errmsg(db_);
    if (log_)
      log_->error("Nexus", "Query execution failed: " + last_error_);
    return false;
  }

  if (log_) {
    std::stringstream label;
    label << "Executed: " << sql.substr(0, 80);
    if (sql.size() > 80)
      label << "...";
    label << " (" << duration_ms << "ms, params=" << params.size() << ")";
    if (duration_ms > 250) {
      log_->warn("Nexus", label.str());
    } else {
      log_->debug("Nexus", label.str());
    }
  }

  return true;
}

bool SqliteDriver::execute_script(const std::string &sql) {
  if (!db_) {
    last_error_ = "Database not open";
    return false;
  }

  char *err_msg = nullptr;
  int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);

  if (rc != SQLITE_OK) {
    last_error_ = err_msg ? err_msg : "Unknown error";
    if (log_)
      log_->error("Nexus", "Script execution failed: " + last_error_);
    if (err_msg)
      sqlite3_free(err_msg);
    return false;
  }

  if (log_)
    log_->debug("Nexus", "Script executed successfully (" +
                std::to_string(sql.length()) + " bytes)");

  return true;
}

QueryResult SqliteDriver::query(const std::string &sql) {
  return exec_query(sql, nullptr);
}

QueryResult SqliteDriver::query(const std::string &sql,
                                const std::vector<std::string> &params) {
  return exec_query(sql, &params);
}

QueryResult SqliteDriver::exec_query(const std::string &sql,
                                     const std::vector<std::string> *params) {
  auto start = std::chrono::steady_clock::now();
  QueryResult result;

  if (!db_) {
    result.error = "Database not open";
    if (log_)
      log_->error("Nexus", result.error);
    return result;
  }

  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    result.error = sqlite3_errmsg(db_);
    if (log_)
      log_->error("Nexus", "Query prepare failed: " + result.error);
    return result;
  }

  // Bind parameters if provided
  if (params) {
    for (size_t i = 0; i < params->size(); ++i) {
      rc = sqlite3_bind_text(stmt, i + 1, (*params)[i].c_str(), -1,
                            SQLITE_TRANSIENT);
      if (rc != SQLITE_OK) {
        result.error = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        if (log_)
          log_->error("Nexus", "Parameter bind failed: " + result.error);
        return result;
      }
    }
  }

  // Get column names
  int col_count = sqlite3_column_count(stmt);
  for (int i = 0; i < col_count; ++i) {
    result.column_names.push_back(sqlite3_column_name(stmt, i));
  }

  // Fetch rows
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    QueryResult::Row row;
    row.columns = result.column_names;
    for (int i = 0; i < col_count; ++i) {
      const char *text =
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, i));
      if (text) {
        row.values.push_back(std::string(text));
      } else {
        row.values.push_back(std::nullopt);
      }
    }
    result.rows.push_back(std::move(row));
  }

  sqlite3_finalize(stmt);

  auto end = std::chrono::steady_clock::now();
  auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();

  if (rc != SQLITE_DONE) {
    result.error = sqlite3_errmsg(db_);
    if (log_)
      log_->error("Nexus", "Query execution failed: " + result.error);
    return result;
  }

  result.ok = true;
  if (log_) {
    std::stringstream label;
    label << "Query returned " << result.rows.size() << " rows (" << duration_ms
          << "ms";
    if (params)
      label << ", params=" << params->size();
    label << ")";
    if (duration_ms > 250) {
      log_->warn("Nexus", label.str());
    } else {
      log_->debug("Nexus", label.str());
    }
  }

  return result;
}

int64_t SqliteDriver::last_insert_id() {
  if (!db_)
    return -1;
  return sqlite3_last_insert_rowid(db_);
}

bool SqliteDriver::begin_transaction() {
  return execute("BEGIN TRANSACTION;");
}

bool SqliteDriver::commit() { return execute("COMMIT;"); }

bool SqliteDriver::rollback() { return execute("ROLLBACK;"); }

std::string SqliteDriver::last_error() const { return last_error_; }

bool SqliteDriver::is_connected() const { return db_ != nullptr; }

bool SqliteDriver::enable_wal_mode() {
  bool ok = execute("PRAGMA journal_mode=WAL;");
  if (log_) {
    if (ok) {
      log_->info("Nexus", "WAL mode enabled");
    } else {
      log_->warn("Nexus", "Failed to enable WAL mode: " + last_error_);
    }
  }
  return ok;
}

} // namespace nazg::nexus

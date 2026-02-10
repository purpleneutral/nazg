#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::nexus {

// Forward declarations
struct QueryResult;

// Abstract database connection interface
class Connection {
public:
  virtual ~Connection() = default;

  // Execute a query that doesn't return rows (INSERT, UPDATE, DELETE, etc.)
  virtual bool execute(const std::string &sql) = 0;

  // Execute a parameterized query (uses ? placeholders)
  virtual bool execute(const std::string &sql,
                       const std::vector<std::string> &params) = 0;

  // Execute multiple SQL statements (for migrations/scripts)
  virtual bool execute_script(const std::string &sql) = 0;

  // Query that returns results
  virtual QueryResult query(const std::string &sql) = 0;
  virtual QueryResult query(const std::string &sql,
                            const std::vector<std::string> &params) = 0;

  // Get last inserted row ID
  virtual int64_t last_insert_id() = 0;

  // Transaction control
  virtual bool begin_transaction() = 0;
  virtual bool commit() = 0;
  virtual bool rollback() = 0;

  // Get last error message
  virtual std::string last_error() const = 0;

  // Check if connected
  virtual bool is_connected() const = 0;
};

// Query result holder
struct QueryResult {
  struct Row {
    std::vector<std::string> columns;
    std::vector<std::optional<std::string>> values;

    std::optional<std::string> get(const std::string &col) const;
    std::optional<int64_t> get_int(const std::string &col) const;
  };

  std::vector<std::string> column_names;
  std::vector<Row> rows;
  bool ok = false;
  std::string error;

  bool empty() const { return rows.empty(); }
  size_t size() const { return rows.size(); }
};

} // namespace nazg::nexus

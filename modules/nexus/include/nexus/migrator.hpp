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

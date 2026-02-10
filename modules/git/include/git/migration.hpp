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

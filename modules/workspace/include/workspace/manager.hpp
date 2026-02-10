#pragma once
#include "workspace/types.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::brain {
class Detector;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::workspace {

class Manager {
public:
  Manager(nexus::Store *store, brain::Detector *detector,
          blackbox::logger *log);

  // Snapshot creation options
  struct SnapshotOptions {
    std::string label;
    std::string trigger_type = "manual";
    bool include_build_dir = true;
    bool include_deps = true;
    std::vector<std::string> extra_paths;
    std::vector<std::string> exclude_patterns;
  };

  // Create a new snapshot
  int64_t create_snapshot(int64_t project_id, const SnapshotOptions &opts);

  // Auto-snapshot before risky operations
  int64_t auto_snapshot(int64_t project_id, const std::string &trigger);

  // List snapshots for a project
  std::vector<WorkspaceSnapshot> list_snapshots(int64_t project_id,
                                                 int limit = 20);

  // Get specific snapshot
  std::optional<WorkspaceSnapshot> get_snapshot(int64_t snapshot_id);
  std::optional<WorkspaceSnapshot> get_snapshot_by_tag(int64_t project_id,
                                                        const std::string &tag);

  // Tag management
  bool tag_snapshot(int64_t snapshot_id, const std::string &tag,
                    const std::string &description = "");
  bool untag(const std::string &tag);
  std::vector<std::string> list_tags(int64_t project_id);

  // Restore options
  struct RestoreOptions {
    std::string restore_type = "smart"; // "smart", "full", "partial"
    bool dry_run = false;
    bool interactive = true;
    std::vector<std::string> include_paths;
    std::vector<std::string> exclude_paths;
  };

  // Restore workspace to a snapshot
  RestoreResult restore(int64_t project_id, int64_t snapshot_id,
                        const RestoreOptions &opts);

  // Diff snapshots
  SnapshotDiff diff(int64_t snapshot_a, int64_t snapshot_b);
  SnapshotDiff diff_current(int64_t project_id, int64_t snapshot_id);

  // Cleanup
  struct PruneOptions {
    bool dry_run = false;
    bool untagged_only = false;
  };

  struct PruneResult {
    int total_candidates = 0;
    int deleted = 0;
    int skipped_tagged = 0;
    std::vector<int64_t> deleted_ids;
    std::vector<int64_t> skipped_tagged_ids;
  };

  PruneResult prune_old_snapshots(int64_t project_id, int keep_count,
                                  PruneOptions opts);
  PruneResult prune_older_than(int64_t project_id, int days_old,
                               PruneOptions opts);

private:
  nexus::Store *store_;
  brain::Detector *detector_;
  blackbox::logger *log_;

  // Helper methods
  std::vector<WorkspaceFile> scan_workspace(const std::string &project_root,
                                             const SnapshotOptions &opts);
  std::string compute_file_hash(const std::string &path);
  FileType detect_file_type(const std::string &path);

  // Enhanced snapshot capture
  std::string compute_build_dir_hash(const std::string &project_root);
  std::string compute_deps_manifest_hash(const std::string &project_root);
  std::string capture_environment();
  std::string capture_system_info();
};

} // namespace nazg::workspace

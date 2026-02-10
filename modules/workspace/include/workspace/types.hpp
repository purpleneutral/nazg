#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nazg::workspace {

// File types in workspace
enum class FileType {
  SOURCE,
  BUILD,
  DEPENDENCY,
  CONFIG,
  UNKNOWN
};

// File entry in a snapshot
struct WorkspaceFile {
  std::string path;          // Relative to project root
  FileType type = FileType::UNKNOWN;
  std::string hash;          // SHA256 of content
  int64_t size = 0;
  int64_t mtime = 0;
};

// Complete workspace snapshot
struct WorkspaceSnapshot {
  int64_t id = 0;
  int64_t project_id = 0;
  int64_t brain_snapshot_id = 0;
  std::string label;
  std::string trigger_type;  // "auto", "manual", "pre-build", etc.
  int64_t timestamp = 0;

  // Extended state
  std::string build_dir_hash;
  std::string deps_manifest_hash;
  std::map<std::string, std::string> env_snapshot;
  std::map<std::string, std::string> system_info;

  // Metadata
  int restore_count = 0;
  bool is_clean_build = false;
  std::string git_commit;
  std::string git_branch;
  std::vector<std::string> tags;

  // Files (populated separately)
  std::vector<WorkspaceFile> files;
};

// Result of a restore operation
struct RestoreResult {
  bool success = false;
  int files_restored = 0;
  int files_skipped = 0;
  std::vector<std::string> errors;
  int64_t duration_ms = 0;
};

// Workspace failure record
struct WorkspaceFailure {
  std::string failure_type;       // "build", "test", "runtime"
  std::string error_signature;    // Hash of error pattern
  std::string error_message;
  int64_t before_snapshot_id = 0;
  int64_t after_snapshot_id = 0;
  std::vector<std::string> changed_files;
  std::map<std::string, std::string> changed_deps;
};

// Change type in diff
enum class ChangeType {
  ADDED,
  MODIFIED,
  DELETED,
  UNCHANGED
};

// File change in a diff
struct FileChange {
  std::string path;
  ChangeType change_type;
  std::string old_hash;
  std::string new_hash;
  int64_t size_diff = 0;
};

// Diff between two snapshots
struct SnapshotDiff {
  int64_t snapshot_a_id = 0;
  int64_t snapshot_b_id = 0;

  // File changes by category
  std::vector<FileChange> source_changes;
  std::vector<FileChange> build_changes;
  std::vector<FileChange> dep_changes;
  std::vector<FileChange> config_changes;
  std::vector<FileChange> other_changes;

  // High-level changes
  std::map<std::string, std::string> dep_upgrades;   // "libfoo": "1.2 -> 1.3"
  std::map<std::string, std::string> env_changes;
  std::map<std::string, std::string> system_info_changes;
  std::vector<std::string> git_commits_between;

  // Analysis
  std::vector<std::string> risk_factors;
  std::vector<std::string> suggestions;
};

} // namespace nazg::workspace

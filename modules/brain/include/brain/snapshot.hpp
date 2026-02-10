#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::brain {

// Snapshot result
struct SnapshotResult {
  std::string tree_hash;
  int file_count = 0;
  int64_t total_bytes = 0;
  bool changed = false;
  std::string previous_hash;
};

// Compute project snapshots for change detection
class Snapshotter {
public:
  explicit Snapshotter(nazg::nexus::Store *store, nazg::blackbox::logger *log = nullptr);

  // Compute snapshot and compare with previous
  SnapshotResult compute(int64_t project_id, const std::string &root_path);

  // Store snapshot in database
  int64_t store_snapshot(int64_t project_id, const SnapshotResult &result);

private:
  struct FileInfo {
    std::string path;
    int64_t size;
    std::string hash;
  };

  // Scan directory and collect file info
  std::vector<FileInfo> scan_files(const std::string &root_path);

  // Compute tree hash from file list
  std::string compute_tree_hash(const std::vector<FileInfo> &files);

  // Check if path should be excluded
  bool should_exclude(const std::string &path);

  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;
};

} // namespace nazg::brain

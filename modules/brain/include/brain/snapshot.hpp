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

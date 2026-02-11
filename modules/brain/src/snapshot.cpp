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

#include "brain/snapshot.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <openssl/evp.h>

namespace fs = std::filesystem;

namespace nazg::brain {

Snapshotter::Snapshotter(nazg::nexus::Store *store, nazg::blackbox::logger *log)
    : store_(store), log_(log) {}

SnapshotResult Snapshotter::compute(int64_t project_id, const std::string &root_path) {
  SnapshotResult result;

  // Scan files
  auto files = scan_files(root_path);
  result.file_count = files.size();
  for (const auto &f : files) {
    result.total_bytes += f.size;
  }

  // Compute tree hash
  result.tree_hash = compute_tree_hash(files);

  // Get previous snapshot
  if (store_) {
    auto prev = store_->latest_snapshot(project_id);
    if (prev) {
      result.previous_hash = prev->tree_hash;
      result.changed = (result.tree_hash != result.previous_hash);
    } else {
      result.changed = true; // First snapshot
    }
  }

  if (log_) {
    log_->info("Brain", "Snapshot: " + std::to_string(result.file_count) + " files, " +
                        std::to_string(result.total_bytes) + " bytes, hash=" +
                        result.tree_hash.substr(0, 8));
    if (!result.previous_hash.empty()) {
      log_->info("Brain", "Changed: " + std::string(result.changed ? "YES" : "NO"));
    }
  }

  return result;
}

int64_t Snapshotter::store_snapshot(int64_t project_id, const SnapshotResult &result) {
  if (!store_) return -1;

  return store_->add_snapshot(project_id, result.tree_hash,
                              result.file_count, result.total_bytes);
}

std::vector<Snapshotter::FileInfo> Snapshotter::scan_files(const std::string &root_path) {
  std::vector<FileInfo> files;

  try {
    for (const auto &entry : fs::recursive_directory_iterator(root_path)) {
      if (!entry.is_regular_file()) continue;

      std::string rel_path = fs::relative(entry.path(), root_path).string();
      if (should_exclude(rel_path)) continue;

      FileInfo info;
      info.path = rel_path;
      info.size = fs::file_size(entry.path());

      // Compute file hash using modern EVP API
      std::ifstream file(entry.path(), std::ios::binary);
      if (file) {
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (ctx) {
          EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

          char buffer[8192];
          while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
            EVP_DigestUpdate(ctx, buffer, file.gcount());
          }

          unsigned char hash[EVP_MAX_MD_SIZE];
          unsigned int hash_len = 0;
          EVP_DigestFinal_ex(ctx, hash, &hash_len);
          EVP_MD_CTX_free(ctx);

          std::ostringstream oss;
          for (unsigned int i = 0; i < hash_len; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
          }
          info.hash = oss.str();
        }
      }

      files.push_back(info);
    }
  } catch (const std::exception &e) {
    if (log_) {
      log_->error("Brain", std::string("Scan error: ") + e.what());
    }
  }

  // Sort by path for consistent hashing
  std::sort(files.begin(), files.end(),
            [](const FileInfo &a, const FileInfo &b) { return a.path < b.path; });

  return files;
}

std::string Snapshotter::compute_tree_hash(const std::vector<FileInfo> &files) {
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) {
    return ""; // Error - return empty hash
  }

  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

  for (const auto &f : files) {
    std::string entry = f.path + ":" + f.hash;
    EVP_DigestUpdate(ctx, entry.c_str(), entry.length());
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  EVP_DigestFinal_ex(ctx, hash, &hash_len);
  EVP_MD_CTX_free(ctx);

  std::ostringstream oss;
  for (unsigned int i = 0; i < hash_len; ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
  }

  return oss.str();
}

bool Snapshotter::should_exclude(const std::string &path) {
  // Exclude build directories
  if (path.find("build/") == 0 || path.find("build-") == 0) return true;
  if (path.find(".cmake/") != std::string::npos) return true;

  // Exclude VCS
  if (path.find(".git/") == 0) return true;
  if (path.find(".svn/") == 0) return true;

  // Exclude dependencies
  if (path.find("node_modules/") == 0) return true;
  if (path.find("target/") == 0 && path.find("rust") != std::string::npos) return true;

  // Exclude generated files
  if (path.find(".o") != std::string::npos) return true;
  if (path.find(".a") != std::string::npos) return true;
  if (path.find(".so") != std::string::npos) return true;

  return false;
}

} // namespace nazg::brain

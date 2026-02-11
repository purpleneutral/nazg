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

#include "workspace/manager.hpp"
#include "blackbox/logger.hpp"
#include "brain/detector.hpp"
#include "nexus/store.hpp"
#include "system/fs.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

namespace ws = nazg::workspace;

std::map<std::string, std::string>
parse_simple_json_map(const std::string &json) {
  std::map<std::string, std::string> result;
  if (json.empty()) {
    return result;
  }

  size_t pos = 0;
  while (true) {
    pos = json.find('"', pos);
    if (pos == std::string::npos) {
      break;
    }

    size_t key_start = pos + 1;
    size_t key_end = json.find('"', key_start);
    if (key_end == std::string::npos) {
      break;
    }
    std::string key = json.substr(key_start, key_end - key_start);

    size_t colon = json.find(':', key_end);
    if (colon == std::string::npos) {
      break;
    }
    size_t value_quote = json.find('"', colon);
    if (value_quote == std::string::npos) {
      break;
    }
    size_t value_start = value_quote + 1;
    size_t value_end = json.find('"', value_start);
    if (value_end == std::string::npos) {
      break;
    }
    std::string value = json.substr(value_start, value_end - value_start);

    result[key] = value;
    pos = value_end + 1;
  }

  return result;
}

fs::path snapshot_storage_root(const std::string &project_root) {
  return fs::path(project_root) / ".nazg" / "workspace" / "snapshots";
}

fs::path snapshot_storage_dir(const std::string &project_root,
                              int64_t snapshot_id) {
  return snapshot_storage_root(project_root) / std::to_string(snapshot_id);
}

bool ensure_directory(const fs::path &path, nazg::blackbox::logger *log) {
  std::error_code ec;
  fs::create_directories(path, ec);
  if (ec) {
    if (log) {
      log->error("Workspace",
                 "Failed to create directory " + path.string() + ": " +
                     ec.message());
    }
    return false;
  }
  return true;
}

std::map<std::string, std::string>
parse_manifest_hashes(const std::string &combined) {
  std::map<std::string, std::string> result;
  size_t pos = 0;
  while (pos < combined.size()) {
    size_t end = combined.find(';', pos);
    std::string entry =
        combined.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    if (!entry.empty()) {
      size_t colon = entry.find(':');
      if (colon != std::string::npos && colon + 1 < entry.size()) {
        std::string key = entry.substr(0, colon);
        std::string value = entry.substr(colon + 1);
        result[key] = value;
      }
    }
    if (end == std::string::npos)
      break;
    pos = end + 1;
  }
  return result;
}

void push_change_for_type(const ws::WorkspaceFile *ref_file,
                          const ws::FileChange &change,
                          ws::SnapshotDiff &diff) {
  ws::FileType type = ref_file ? ref_file->type : ws::FileType::UNKNOWN;
  switch (type) {
  case ws::FileType::SOURCE:
    diff.source_changes.push_back(change);
    break;
  case ws::FileType::BUILD:
    diff.build_changes.push_back(change);
    break;
  case ws::FileType::DEPENDENCY:
    diff.dep_changes.push_back(change);
    break;
  case ws::FileType::CONFIG:
    diff.config_changes.push_back(change);
    break;
  default:
    diff.other_changes.push_back(change);
    break;
  }
}

ws::SnapshotDiff compute_snapshot_diff(const ws::WorkspaceSnapshot &from,
                                       const ws::WorkspaceSnapshot &to) {
  ws::SnapshotDiff diff;
  diff.snapshot_a_id = from.id;
  diff.snapshot_b_id = to.id;

  std::unordered_map<std::string, const ws::WorkspaceFile *> from_map;
  std::unordered_map<std::string, const ws::WorkspaceFile *> to_map;
  std::unordered_set<std::string> all_paths;

  for (const auto &file : from.files) {
    from_map[file.path] = &file;
    all_paths.insert(file.path);
  }
  for (const auto &file : to.files) {
    to_map[file.path] = &file;
    all_paths.insert(file.path);
  }

  for (const auto &path : all_paths) {
    const ws::WorkspaceFile *old_file =
        from_map.count(path) ? from_map.at(path) : nullptr;
    const ws::WorkspaceFile *new_file =
        to_map.count(path) ? to_map.at(path) : nullptr;

    if (old_file && new_file) {
      if (old_file->hash == new_file->hash) {
        continue;
      }

      ws::FileChange change;
      change.path = path;
      change.change_type = ws::ChangeType::MODIFIED;
      change.old_hash = old_file->hash;
      change.new_hash = new_file->hash;
      change.size_diff = new_file->size - old_file->size;
      push_change_for_type(new_file, change, diff);
    } else if (!old_file && new_file) {
      ws::FileChange change;
      change.path = path;
      change.change_type = ws::ChangeType::ADDED;
      change.old_hash = "";
      change.new_hash = new_file->hash;
      change.size_diff = new_file->size;
      push_change_for_type(new_file, change, diff);
    } else if (old_file && !new_file) {
      ws::FileChange change;
      change.path = path;
      change.change_type = ws::ChangeType::DELETED;
      change.old_hash = old_file->hash;
      change.new_hash = "";
      change.size_diff = -old_file->size;
      push_change_for_type(old_file, change, diff);
    }
  }

  // Environment differences
  std::unordered_set<std::string> env_keys;
  for (const auto &kv : from.env_snapshot) {
    env_keys.insert(kv.first);
  }
  for (const auto &kv : to.env_snapshot) {
    env_keys.insert(kv.first);
  }
  for (const auto &key : env_keys) {
    std::string old_val =
        from.env_snapshot.count(key) ? from.env_snapshot.at(key) : "(missing)";
    std::string new_val =
        to.env_snapshot.count(key) ? to.env_snapshot.at(key) : "(missing)";
    if (old_val != new_val) {
      diff.env_changes[key] = old_val + " -> " + new_val;
    }
  }

  // System info differences
  std::unordered_set<std::string> sys_keys;
  for (const auto &kv : from.system_info) {
    sys_keys.insert(kv.first);
  }
  for (const auto &kv : to.system_info) {
    sys_keys.insert(kv.first);
  }
  for (const auto &key : sys_keys) {
    std::string old_val =
        from.system_info.count(key) ? from.system_info.at(key) : "(missing)";
    std::string new_val =
        to.system_info.count(key) ? to.system_info.at(key) : "(missing)";
    if (old_val != new_val) {
      diff.system_info_changes[key] = old_val + " -> " + new_val;
    }
  }

  // Dependency manifest differences
  auto old_manifests = parse_manifest_hashes(from.deps_manifest_hash);
  auto new_manifests = parse_manifest_hashes(to.deps_manifest_hash);
  std::unordered_set<std::string> manifest_keys;
  for (const auto &kv : old_manifests) {
    manifest_keys.insert(kv.first);
  }
  for (const auto &kv : new_manifests) {
    manifest_keys.insert(kv.first);
  }
  for (const auto &key : manifest_keys) {
    std::string old_val =
        old_manifests.count(key) ? old_manifests.at(key) : "(missing)";
    std::string new_val =
        new_manifests.count(key) ? new_manifests.at(key) : "(missing)";
    if (old_val != new_val) {
      diff.dep_upgrades[key] = old_val + " -> " + new_val;
    }
  }

  return diff;
}

} // namespace

namespace nazg::workspace {

Manager::Manager(nexus::Store *store, brain::Detector *detector,
                 blackbox::logger *log)
    : store_(store), detector_(detector), log_(log) {}

int64_t Manager::create_snapshot(int64_t project_id,
                                  const SnapshotOptions &opts) {
  if (!store_) {
    if (log_) {
      log_->error("Workspace", "Cannot create snapshot: no store");
    }
    return 0;
  }

  // Get project info
  auto project = store_->get_project(project_id);
  if (!project) {
    if (log_) {
      log_->error("Workspace", "Cannot create snapshot: project not found");
    }
    return 0;
  }

  std::string project_root = project->root_path;

  if (log_) {
    log_->info("Workspace",
               "Creating snapshot for project: " + project_root);
  }

  // Scan workspace files
  auto files = scan_workspace(project_root, opts);

  if (log_) {
    log_->info("Workspace",
               "Scanned " + std::to_string(files.size()) + " files");
  }

  // Get git info if available
  std::string git_commit, git_branch;
  // TODO: Integrate with git module to get current commit/branch

  // Detect build directory and compute hash
  std::string build_dir_hash;
  if (opts.include_build_dir) {
    build_dir_hash = compute_build_dir_hash(project_root);
  }

  // Detect dependency manifests and compute hash
  std::string deps_manifest_hash;
  if (opts.include_deps) {
    deps_manifest_hash = compute_deps_manifest_hash(project_root);
  }

  // Capture environment variables
  std::string env_snapshot_json = capture_environment();

  // Capture system info
  std::string system_info_json = capture_system_info();

  // Create snapshot record
  int64_t snapshot_id = store_->add_workspace_snapshot(
      project_id, -1, // brain_snapshot_id (-1 means NULL, TODO: link to brain snapshot)
      opts.label, opts.trigger_type, build_dir_hash, deps_manifest_hash,
      env_snapshot_json, system_info_json,
      false, // is_clean_build (TODO: detect)
      git_commit, git_branch);

  if (snapshot_id == 0) {
    if (log_) {
      log_->error("Workspace", "Failed to create snapshot record");
    }
    return 0;
  }

  // Store file records
  for (const auto &file : files) {
    store_->add_workspace_file(snapshot_id, file.path,
                                [](FileType t) -> std::string {
                                  switch (t) {
                                  case FileType::SOURCE:
                                    return "source";
                                  case FileType::BUILD:
                                    return "build";
                                  case FileType::DEPENDENCY:
                                    return "dep";
                                  case FileType::CONFIG:
                                    return "config";
                                  default:
                                    return "unknown";
                                  }
                                }(file.type),
                                file.hash, file.size, file.mtime);
  }

  // Persist file contents for restore
  fs::path snapshot_dir = snapshot_storage_dir(project_root, snapshot_id);
  if (ensure_directory(snapshot_dir, log_)) {
    for (const auto &file : files) {
      fs::path source = fs::path(project_root) / file.path;
      if (!fs::exists(source)) {
        if (log_) {
          log_->warn("Workspace",
                     "File missing during snapshot copy: " + source.string());
        }
        continue;
      }

      fs::path destination = snapshot_dir / file.path;
      if (!ensure_directory(destination.parent_path(), log_)) {
        continue;
      }

      std::error_code ec;
      fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
      if (ec && log_) {
        log_->warn("Workspace",
                   "Failed to copy " + source.string() + " to snapshot: " +
                       ec.message());
      }
    }
  }

  if (log_) {
    log_->info("Workspace",
               "Created snapshot #" + std::to_string(snapshot_id));
  }

  return snapshot_id;
}

int64_t Manager::auto_snapshot(int64_t project_id,
                                const std::string &trigger) {
  SnapshotOptions opts;
  opts.label = "auto-" + trigger;
  opts.trigger_type = "auto";
  return create_snapshot(project_id, opts);
}

std::vector<WorkspaceSnapshot>
Manager::list_snapshots(int64_t project_id, int limit) {
  std::vector<WorkspaceSnapshot> snapshots;

  if (!store_) {
    return snapshots;
  }

  auto rows = store_->list_workspace_snapshots(project_id, limit);

  for (const auto &row : rows) {
    WorkspaceSnapshot snap;
    snap.id = std::stoll(row.at("id"));
    snap.project_id = std::stoll(row.at("project_id"));

    if (row.count("brain_snapshot_id") && !row.at("brain_snapshot_id").empty()) {
      snap.brain_snapshot_id = std::stoll(row.at("brain_snapshot_id"));
    }

    snap.label = row.at("label");
    snap.trigger_type = row.at("trigger_type");
    snap.timestamp = std::stoll(row.at("timestamp"));

    if (row.count("restore_count")) {
      snap.restore_count = std::stoi(row.at("restore_count"));
    }

    if (row.count("is_clean_build")) {
      snap.is_clean_build = row.at("is_clean_build") == "1";
    }

    if (row.count("git_commit")) {
      snap.git_commit = row.at("git_commit");
    }

    if (row.count("git_branch")) {
      snap.git_branch = row.at("git_branch");
    }

    if (row.count("build_dir_hash")) {
      snap.build_dir_hash = row.at("build_dir_hash");
    }

    if (row.count("deps_manifest_hash")) {
      snap.deps_manifest_hash = row.at("deps_manifest_hash");
    }

    if (row.count("env_snapshot")) {
      snap.env_snapshot = parse_simple_json_map(row.at("env_snapshot"));
    }

    if (row.count("system_info")) {
      snap.system_info = parse_simple_json_map(row.at("system_info"));
    }

    snapshots.push_back(snap);
  }

  return snapshots;
}

std::optional<WorkspaceSnapshot> Manager::get_snapshot(int64_t snapshot_id) {
  if (!store_) {
    return std::nullopt;
  }

  auto row = store_->get_workspace_snapshot(snapshot_id);
  if (!row) {
    return std::nullopt;
  }

  WorkspaceSnapshot snap;
  snap.id = std::stoll(row->at("id"));
  snap.project_id = std::stoll(row->at("project_id"));

  if (row->count("brain_snapshot_id") &&
      !row->at("brain_snapshot_id").empty()) {
    snap.brain_snapshot_id = std::stoll(row->at("brain_snapshot_id"));
  }

  snap.label = row->at("label");
  snap.trigger_type = row->at("trigger_type");
  snap.timestamp = std::stoll(row->at("timestamp"));

  if (row->count("restore_count")) {
    snap.restore_count = std::stoi(row->at("restore_count"));
  }

  if (row->count("is_clean_build")) {
    snap.is_clean_build = row->at("is_clean_build") == "1";
  }

  if (row->count("git_commit")) {
    snap.git_commit = row->at("git_commit");
  }

  if (row->count("git_branch")) {
    snap.git_branch = row->at("git_branch");
  }

  if (row->count("build_dir_hash")) {
    snap.build_dir_hash = row->at("build_dir_hash");
  }

  if (row->count("deps_manifest_hash")) {
    snap.deps_manifest_hash = row->at("deps_manifest_hash");
  }

  if (row->count("env_snapshot")) {
    snap.env_snapshot = parse_simple_json_map(row->at("env_snapshot"));
  }

  if (row->count("system_info")) {
    snap.system_info = parse_simple_json_map(row->at("system_info"));
  }

  // Load tags for this snapshot
  auto tag_rows = store_->list_workspace_tags(snap.project_id);
  for (const auto &tag_row : tag_rows) {
    if (std::stoll(tag_row.at("snapshot_id")) == snap.id) {
      snap.tags.push_back(tag_row.at("tag_name"));
    }
  }

  // Load files
  auto file_rows = store_->get_workspace_files(snapshot_id);
  for (const auto &frow : file_rows) {
    WorkspaceFile wf;
    wf.path = frow.at("file_path");
    wf.hash = frow.at("file_hash");
    wf.size = std::stoll(frow.at("file_size"));
    wf.mtime = std::stoll(frow.at("mtime"));

    std::string type_str = frow.at("file_type");
    if (type_str == "source")
      wf.type = FileType::SOURCE;
    else if (type_str == "build")
      wf.type = FileType::BUILD;
    else if (type_str == "dep")
      wf.type = FileType::DEPENDENCY;
    else if (type_str == "config")
      wf.type = FileType::CONFIG;
    else
      wf.type = FileType::UNKNOWN;

    snap.files.push_back(wf);
  }

  return snap;
}

std::optional<WorkspaceSnapshot>
Manager::get_snapshot_by_tag(int64_t project_id, const std::string &tag) {
  if (!store_) {
    return std::nullopt;
  }

  auto snapshot_id = store_->get_snapshot_by_tag(project_id, tag);
  if (!snapshot_id) {
    return std::nullopt;
  }

  return get_snapshot(*snapshot_id);
}

bool Manager::tag_snapshot(int64_t snapshot_id, const std::string &tag,
                            const std::string &description) {
  if (!store_) {
    return false;
  }

  // Get snapshot to find project_id
  auto snapshot = get_snapshot(snapshot_id);
  if (!snapshot) {
    if (log_) {
      log_->error("Workspace", "Cannot tag: snapshot not found");
    }
    return false;
  }

  return store_->tag_workspace_snapshot(snapshot->project_id, snapshot_id, tag,
                                        description);
}

bool Manager::untag(const std::string &/*tag*/) {
  // TODO: Need to know project_id to untag
  // For now, this is incomplete
  if (log_) {
    log_->warn("Workspace", "untag not fully implemented yet");
  }
  return false;
}

std::vector<std::string> Manager::list_tags(int64_t project_id) {
  std::vector<std::string> tags;

  if (!store_) {
    return tags;
  }

  auto rows = store_->list_workspace_tags(project_id);
  for (const auto &row : rows) {
    tags.push_back(row.at("tag_name"));
  }

  return tags;
}

RestoreResult Manager::restore(int64_t project_id, int64_t snapshot_id,
                                const RestoreOptions &opts) {
  RestoreResult result;
  result.success = false;
  result.files_restored = 0;
  result.files_skipped = 0;
  result.duration_ms = 0;

  auto start_time = std::chrono::steady_clock::now();

  if (!store_) {
    result.errors.push_back("Workspace store not available");
    return result;
  }

  auto project = store_->get_project(project_id);
  if (!project) {
    result.errors.push_back("Project not found");
    return result;
  }

  auto snapshot = get_snapshot(snapshot_id);
  if (!snapshot) {
    result.errors.push_back("Snapshot not found");
    return result;
  }

  if (snapshot->project_id != project_id) {
    result.errors.push_back("Snapshot does not belong to this project");
    return result;
  }

  fs::path project_root = project->root_path;
  fs::path snapshot_dir = snapshot_storage_dir(project_root, snapshot_id);
  if (!fs::exists(snapshot_dir)) {
    result.errors.push_back("Snapshot data not found at " + snapshot_dir.string());
    return result;
  }

  auto matches_prefix = [](const std::string &path,
                           const std::vector<std::string> &patterns) {
    if (patterns.empty()) {
      return true;
    }
    for (const auto &pat : patterns) {
      if (pat.empty()) {
        continue;
      }
      if (path == pat) {
        return true;
      }
      if (path.size() > pat.size() &&
          std::equal(pat.begin(), pat.end(), path.begin()) &&
          path[pat.size()] == '/') {
        return true;
      }
    }
    return false;
  };

  auto is_excluded = [](const std::string &path,
                        const std::vector<std::string> &patterns) {
    for (const auto &pat : patterns) {
      if (pat.empty()) {
        continue;
      }
      if (path == pat) {
        return true;
      }
      if (path.size() > pat.size() &&
          std::equal(pat.begin(), pat.end(), path.begin()) &&
          path[pat.size()] == '/') {
        return true;
      }
    }
    return false;
  };

  auto should_include = [&](const std::string &path) {
    if (!opts.include_paths.empty() && !matches_prefix(path, opts.include_paths)) {
      return false;
    }
    if (is_excluded(path, opts.exclude_paths)) {
      return false;
    }
    return true;
  };

  bool dry_run = opts.dry_run;
  std::string mode = opts.restore_type.empty() ? "full" : opts.restore_type;
  std::transform(mode.begin(), mode.end(), mode.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (!opts.include_paths.empty() && mode == "full") {
    mode = "partial";
  }

  if (mode == "partial" && opts.include_paths.empty()) {
    result.errors.push_back("Partial restore requires at least one --include/--only path");
    return result;
  }

  if (mode != "full" && mode != "smart" && mode != "partial") {
    result.errors.push_back("Unknown restore mode: " + mode);
    return result;
  }

  std::unordered_map<std::string, const ws::WorkspaceFile *> file_lookup;
  for (const auto &file : snapshot->files) {
    file_lookup[file.path] = &file;
  }

  std::unordered_set<std::string> restore_set;
  std::vector<std::string> restore_paths;
  int filtered_out = 0;

  auto add_candidate = [&](const std::string &path) {
    if (!should_include(path)) {
      filtered_out++;
      return;
    }
    if (restore_set.insert(path).second) {
      restore_paths.push_back(path);
    }
  };

  if (mode == "full" || mode == "partial") {
    for (const auto &file : snapshot->files) {
      add_candidate(file.path);
    }
  } else { // smart
    SnapshotDiff diff_view = diff_current(project_id, snapshot_id);

    auto consider_changes = [&](const std::vector<workspace::FileChange> &changes,
                                bool include_all) {
      for (const auto &change : changes) {
        auto it = file_lookup.find(change.path);
        if (it == file_lookup.end()) {
          continue;
        }
        bool include = include_all &&
                       (change.change_type == workspace::ChangeType::ADDED ||
                        change.change_type == workspace::ChangeType::MODIFIED);
        if (change.change_type == workspace::ChangeType::ADDED) {
          include = true;
        }
        if (!opts.include_paths.empty() &&
            matches_prefix(change.path, opts.include_paths)) {
          include = true;
        }
        if (include) {
          add_candidate(change.path);
        }
      }
    };

    consider_changes(diff_view.build_changes, true);
    consider_changes(diff_view.dep_changes, true);
    consider_changes(diff_view.config_changes, true);
    consider_changes(diff_view.other_changes, true);

    // Source files: restore only if they no longer exist locally, or are explicitly requested
    for (const auto &change : diff_view.source_changes) {
      auto it = file_lookup.find(change.path);
      if (it == file_lookup.end()) {
        continue;
      }
      bool include = false;
      if (change.change_type == workspace::ChangeType::ADDED) {
        include = true;
      } else if (!opts.include_paths.empty() &&
                 matches_prefix(change.path, opts.include_paths)) {
        include = true;
      }
      if (include) {
        add_candidate(change.path);
      }
    }
  }

  if (restore_paths.empty() && filtered_out == 0) {
    result.success = true;
    return result;
  }

  std::sort(restore_paths.begin(), restore_paths.end());

  int64_t restore_record_id = 0;
  auto restore_start_clock = std::chrono::steady_clock::now();
  if (!dry_run) {
    restore_record_id =
        store_->begin_workspace_restore(project_id, snapshot_id, mode,
                                        "workspace restore");
  }

  result.files_skipped += filtered_out;

  for (const auto &relative : restore_paths) {
    auto info_it = file_lookup.find(relative);
    if (info_it == file_lookup.end()) {
      result.files_skipped++;
      continue;
    }

    fs::path source = snapshot_dir / relative;
    if (!fs::exists(source)) {
      result.errors.push_back("Missing snapshot file: " + source.string());
      result.files_skipped++;
      continue;
    }

    if (dry_run) {
      result.files_restored++;
      result.files_skipped++;
      continue;
    }

    fs::path target = fs::path(project_root) / relative;
    if (!ensure_directory(target.parent_path(), log_)) {
      result.errors.push_back("Failed to prepare directory for " +
                              target.string());
      result.files_skipped++;
      continue;
    }

    std::error_code ec;
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      result.errors.push_back("Failed to restore " + relative + ": " +
                              ec.message());
      result.files_skipped++;
      continue;
    }

    result.files_restored++;
  }

  auto end_time = std::chrono::steady_clock::now();
  result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           end_time - start_time)
                           .count();

  if (!dry_run) {
    if (restore_record_id > 0) {
      store_->finish_workspace_restore(
          restore_record_id, result.errors.empty(), result.files_restored,
          std::chrono::duration_cast<std::chrono::milliseconds>(
              end_time - restore_start_clock)
              .count());
    }

    if (result.errors.empty()) {
      store_->update_workspace_snapshot_restore_count(snapshot_id);
    }
  }

  result.success = result.errors.empty();
  return result;
}

SnapshotDiff Manager::diff(int64_t snapshot_a, int64_t snapshot_b) {
  SnapshotDiff diff;
  diff.snapshot_a_id = snapshot_a;
  diff.snapshot_b_id = snapshot_b;

  auto snap_a = get_snapshot(snapshot_a);
  auto snap_b = get_snapshot(snapshot_b);
  if (!snap_a || !snap_b) {
    if (log_) {
      log_->warn("Workspace", "Unable to diff snapshots: snapshot missing");
    }
    return diff;
  }

  diff = compute_snapshot_diff(*snap_a, *snap_b);
  diff.snapshot_a_id = snapshot_a;
  diff.snapshot_b_id = snapshot_b;

  return diff;
}

SnapshotDiff Manager::diff_current(int64_t project_id, int64_t snapshot_id) {
  SnapshotDiff diff;
  diff.snapshot_a_id = 0;
  diff.snapshot_b_id = snapshot_id;

  if (!store_) {
    return diff;
  }

  auto project = store_->get_project(project_id);
  if (!project) {
    if (log_) {
      log_->warn("Workspace", "Cannot diff current state: project not found");
    }
    return diff;
  }

  auto snapshot = get_snapshot(snapshot_id);
  if (!snapshot) {
    if (log_) {
      log_->warn("Workspace", "Cannot diff current state: snapshot not found");
    }
    return diff;
  }

  SnapshotOptions scan_opts;
  scan_opts.include_build_dir = true;
  scan_opts.include_deps = true;

  WorkspaceSnapshot current;
  current.id = 0;
  current.project_id = project_id;
  current.files = scan_workspace(project->root_path, scan_opts);
  current.build_dir_hash = compute_build_dir_hash(project->root_path);
  current.deps_manifest_hash = compute_deps_manifest_hash(project->root_path);
  current.env_snapshot = parse_simple_json_map(capture_environment());
  current.system_info = parse_simple_json_map(capture_system_info());

  diff = compute_snapshot_diff(current, *snapshot);
  diff.snapshot_a_id = 0;
  diff.snapshot_b_id = snapshot_id;
  return diff;
}

Manager::PruneResult
Manager::prune_old_snapshots(int64_t project_id, int keep_count,
                             PruneOptions opts) {
  PruneResult result;

  if (!store_ || keep_count < 0) {
    return result;
  }

  auto snapshots = list_snapshots(project_id, 1000);
  if (snapshots.size() <= static_cast<size_t>(keep_count)) {
    if (log_) {
      log_->info("Workspace",
                 "No snapshots to prune (have " +
                     std::to_string(snapshots.size()) + ", keeping " +
                     std::to_string(keep_count) + ")");
    }
    return result;
  }

  std::unordered_map<int64_t, std::vector<std::string>> tag_map;
  for (const auto &tag_row : store_->list_workspace_tags(project_id)) {
    int64_t snapshot_id = std::stoll(tag_row.at("snapshot_id"));
    tag_map[snapshot_id].push_back(tag_row.at("tag_name"));
  }

  result.total_candidates =
      static_cast<int>(snapshots.size() - static_cast<size_t>(keep_count));

  for (size_t i = keep_count; i < snapshots.size(); ++i) {
    const auto &snap = snapshots[i];
    bool has_tag = tag_map.count(snap.id) > 0;

    if (has_tag && opts.untagged_only) {
      result.skipped_tagged++;
      result.skipped_tagged_ids.push_back(snap.id);
      continue;
    }

    if (opts.dry_run) {
      result.deleted_ids.push_back(snap.id);
      if (log_) {
        log_->info("Workspace", "[dry-run] would delete snapshot #" +
                                    std::to_string(snap.id));
      }
      continue;
    }

    if (store_->delete_workspace_snapshot(snap.id)) {
      result.deleted_ids.push_back(snap.id);
      if (log_) {
        log_->info("Workspace",
                   "Deleted snapshot #" + std::to_string(snap.id));
      }
    } else if (log_) {
      log_->error("Workspace", "Failed to delete snapshot #" +
                                   std::to_string(snap.id));
    }
  }

  result.deleted = static_cast<int>(result.deleted_ids.size());
  return result;
}

Manager::PruneResult
Manager::prune_older_than(int64_t project_id, int days_old,
                          PruneOptions opts) {
  PruneResult result;

  if (!store_ || days_old <= 0) {
    return result;
  }

  auto now = std::chrono::system_clock::now();
  auto cutoff = now - std::chrono::hours(24 * days_old);
  int64_t cutoff_timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(
          cutoff.time_since_epoch())
          .count();

  auto snapshots = list_snapshots(project_id, 1000);
  std::unordered_map<int64_t, std::vector<std::string>> tag_map;
  for (const auto &tag_row : store_->list_workspace_tags(project_id)) {
    int64_t snapshot_id = std::stoll(tag_row.at("snapshot_id"));
    tag_map[snapshot_id].push_back(tag_row.at("tag_name"));
  }

  for (const auto &snap : snapshots) {
    if (snap.timestamp >= cutoff_timestamp) {
      continue;
    }

    result.total_candidates++;
    bool has_tag = tag_map.count(snap.id) > 0;
    if (has_tag && opts.untagged_only) {
      result.skipped_tagged++;
      result.skipped_tagged_ids.push_back(snap.id);
      continue;
    }

    if (opts.dry_run) {
      result.deleted_ids.push_back(snap.id);
      if (log_) {
        log_->info("Workspace", "[dry-run] would delete snapshot #" +
                                    std::to_string(snap.id));
      }
      continue;
    }

    if (store_->delete_workspace_snapshot(snap.id)) {
      result.deleted_ids.push_back(snap.id);
      if (log_) {
        log_->info("Workspace",
                   "Deleted snapshot #" + std::to_string(snap.id));
      }
    } else if (log_) {
      log_->error("Workspace", "Failed to delete snapshot #" +
                                   std::to_string(snap.id));
    }
  }

  result.deleted = static_cast<int>(result.deleted_ids.size());
  return result;
}

// Helper methods

std::vector<WorkspaceFile>
Manager::scan_workspace(const std::string &project_root,
                        const SnapshotOptions &opts) {
  std::vector<WorkspaceFile> files;

  try {
    for (const auto &entry : fs::recursive_directory_iterator(project_root)) {
      if (!entry.is_regular_file()) {
        continue;
      }

      std::string path = entry.path().string();
      std::string relative_path =
          fs::relative(entry.path(), project_root).string();

      // Skip hidden files and directories
      if (relative_path.find("/.") != std::string::npos ||
          relative_path[0] == '.') {
        continue;
      }

      // Skip build directory if not included
      if (!opts.include_build_dir &&
          (relative_path.find("build/") == 0 ||
           relative_path.find("build-") == 0)) {
        continue;
      }

      // Skip common exclude patterns
      bool skip = false;
      for (const auto &pattern : opts.exclude_patterns) {
        if (relative_path.find(pattern) != std::string::npos) {
          skip = true;
          break;
        }
      }
      if (skip)
        continue;

      // Create file entry
      WorkspaceFile wf;
      wf.path = relative_path;
      wf.type = detect_file_type(relative_path);
      wf.size = entry.file_size();
      wf.mtime =
          std::chrono::duration_cast<std::chrono::seconds>(
              entry.last_write_time().time_since_epoch())
              .count();

      // Compute hash
      wf.hash = compute_file_hash(path);

      files.push_back(wf);
    }
  } catch (const fs::filesystem_error &e) {
    if (log_) {
      log_->error("Workspace", "Error scanning workspace: " +
                                   std::string(e.what()));
    }
  }

  return files;
}

std::string Manager::compute_file_hash(const std::string &path) {
  // Simple hash implementation for now
  // TODO: Use proper SHA256 implementation
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return "";
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  std::string content = ss.str();

  // Simple checksum (replace with proper hash later)
  size_t hash = 0;
  for (char c : content) {
    hash = hash * 31 + c;
  }

  std::ostringstream hash_ss;
  hash_ss << std::hex << hash;
  return hash_ss.str();
}

FileType Manager::detect_file_type(const std::string &path) {
  // Detect file type based on path and extension
  if (path.find("/build/") != std::string::npos ||
      path.find("/build-") != std::string::npos ||
      path.find(".o") != std::string::npos ||
      path.find(".a") != std::string::npos ||
      path.find(".so") != std::string::npos) {
    return FileType::BUILD;
  }

  if (path.find("CMakeLists.txt") != std::string::npos ||
      path.find("Makefile") != std::string::npos ||
      path.find(".toml") != std::string::npos ||
      path.find(".yml") != std::string::npos ||
      path.find(".yaml") != std::string::npos ||
      path.find(".json") != std::string::npos) {
    return FileType::CONFIG;
  }

  if (path.find("package.json") != std::string::npos ||
      path.find("Cargo.lock") != std::string::npos ||
      path.find("requirements.txt") != std::string::npos ||
      path.find("go.sum") != std::string::npos) {
    return FileType::DEPENDENCY;
  }

  // Source file extensions
  if (path.find(".cpp") != std::string::npos ||
      path.find(".hpp") != std::string::npos ||
      path.find(".c") != std::string::npos ||
      path.find(".h") != std::string::npos ||
      path.find(".py") != std::string::npos ||
      path.find(".js") != std::string::npos ||
      path.find(".ts") != std::string::npos ||
      path.find(".rs") != std::string::npos ||
      path.find(".go") != std::string::npos) {
    return FileType::SOURCE;
  }

  return FileType::UNKNOWN;
}

std::string Manager::compute_build_dir_hash(const std::string &project_root) {
  // Find common build directories
  std::vector<std::string> build_dirs = {"build", "build-debug", "build-release",
                                          "cmake-build-debug", "cmake-build-release",
                                          "target", "dist", "out"};

  std::ostringstream combined;
  for (const auto &dir_name : build_dirs) {
    fs::path build_path = fs::path(project_root) / dir_name;
    if (fs::exists(build_path) && fs::is_directory(build_path)) {
      try {
        // Count files and compute simple checksum
        int file_count = 0;
        int64_t total_size = 0;
        for (const auto &entry : fs::recursive_directory_iterator(build_path)) {
          if (entry.is_regular_file()) {
            file_count++;
            total_size += entry.file_size();
          }
        }
        combined << dir_name << ":" << file_count << ":" << total_size << ";";
      } catch (const fs::filesystem_error &) {
        // Skip directories we can't read
      }
    }
  }

  return combined.str();
}

std::string Manager::compute_deps_manifest_hash(const std::string &project_root) {
  // Common dependency manifest files
  std::vector<std::string> manifest_files = {
      "package.json", "package-lock.json", "yarn.lock",
      "Cargo.toml", "Cargo.lock",
      "requirements.txt", "Pipfile", "Pipfile.lock", "poetry.lock",
      "go.mod", "go.sum",
      "pom.xml", "build.gradle",
      "composer.json", "composer.lock"
  };

  std::ostringstream combined;
  for (const auto &filename : manifest_files) {
    fs::path manifest_path = fs::path(project_root) / filename;
    if (fs::exists(manifest_path) && fs::is_regular_file(manifest_path)) {
      std::string hash = compute_file_hash(manifest_path.string());
      combined << filename << ":" << hash << ";";
    }
  }

  return combined.str();
}

std::string Manager::capture_environment() {
  // Capture relevant build-related environment variables
  std::vector<std::string> relevant_vars = {
      "CC", "CXX", "CFLAGS", "CXXFLAGS", "LDFLAGS",
      "CMAKE_BUILD_TYPE", "BUILD_TYPE",
      "PATH", "LD_LIBRARY_PATH",
      "CARGO_HOME", "RUSTC", "RUSTFLAGS",
      "GOPATH", "GOROOT",
      "JAVA_HOME", "MAVEN_HOME",
      "NODE_ENV", "NPM_CONFIG_PREFIX"
  };

  std::ostringstream json;
  json << "{";
  bool first = true;
  for (const auto &var : relevant_vars) {
    if (const char *value = std::getenv(var.c_str())) {
      if (!first) json << ",";
      json << "\"" << var << "\":\"" << value << "\"";
      first = false;
    }
  }
  json << "}";

  return json.str();
}

std::string Manager::capture_system_info() {
  std::ostringstream json;
  json << "{";

  // Try to get compiler versions
  bool first = true;

  // Check for g++
  FILE *gcc_pipe = popen("g++ --version 2>/dev/null | head -1", "r");
  if (gcc_pipe) {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), gcc_pipe)) {
      std::string gcc_version = buffer;
      gcc_version.erase(gcc_version.find_last_not_of(" \n\r\t") + 1);
      if (!first) json << ",";
      json << "\"g++\":\"" << gcc_version << "\"";
      first = false;
    }
    pclose(gcc_pipe);
  }

  // Check for clang++
  FILE *clang_pipe = popen("clang++ --version 2>/dev/null | head -1", "r");
  if (clang_pipe) {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), clang_pipe)) {
      std::string clang_version = buffer;
      clang_version.erase(clang_version.find_last_not_of(" \n\r\t") + 1);
      if (!first) json << ",";
      json << "\"clang++\":\"" << clang_version << "\"";
      first = false;
    }
    pclose(clang_pipe);
  }

  // Check for cmake
  FILE *cmake_pipe = popen("cmake --version 2>/dev/null | head -1", "r");
  if (cmake_pipe) {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), cmake_pipe)) {
      std::string cmake_version = buffer;
      cmake_version.erase(cmake_version.find_last_not_of(" \n\r\t") + 1);
      if (!first) json << ",";
      json << "\"cmake\":\"" << cmake_version << "\"";
      first = false;
    }
    pclose(cmake_pipe);
  }

  // Get OS info
  FILE *os_pipe = popen("uname -sr 2>/dev/null", "r");
  if (os_pipe) {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), os_pipe)) {
      std::string os_info = buffer;
      os_info.erase(os_info.find_last_not_of(" \n\r\t") + 1);
      if (!first) json << ",";
      json << "\"os\":\"" << os_info << "\"";
      first = false;
    }
    pclose(os_pipe);
  }

  json << "}";
  return json.str();
}

} // namespace nazg::workspace

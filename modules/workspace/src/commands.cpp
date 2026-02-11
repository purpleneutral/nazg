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

#include "workspace/commands.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"
#include "workspace/manager.hpp"
#include "nexus/store.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <algorithm>
#include <unistd.h>

namespace nazg::workspace {

// Get current working directory
static std::string get_cwd() {
  char buf[4096];
  if (::getcwd(buf, sizeof(buf))) {
    return std::string(buf);
  }
  return ".";
}

// Helper function to format timestamp
static std::string format_timestamp(int64_t timestamp) {
  std::time_t time = static_cast<std::time_t>(timestamp);
  std::tm *tm = std::localtime(&time);
  std::ostringstream ss;
  ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

// Helper function to format time ago
static std::string time_ago(int64_t timestamp) {
  auto now = std::chrono::system_clock::now();
  auto now_timestamp =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count();

  int64_t diff = now_timestamp - timestamp;

  if (diff < 60) {
    return std::to_string(diff) + "s ago";
  } else if (diff < 3600) {
    return std::to_string(diff / 60) + "m ago";
  } else if (diff < 86400) {
    return std::to_string(diff / 3600) + "h ago";
  } else {
    return std::to_string(diff / 86400) + "d ago";
  }
}

static int cmd_workspace_snapshot(const directive::command_context &cmd_ctx,
                                   const directive::context &ctx) {
  // Parse arguments from cmd_ctx starting at position 3
  // argv[0] = "nazg", argv[1] = "workspace", argv[2] = "snapshot", argv[3+] = options
  std::vector<std::string> args;
  for (int i = 3; i < cmd_ctx.argc; ++i) {
    args.push_back(cmd_ctx.argv[i]);
  }
  // Parse options
  std::string label;
  std::string tag;
  bool no_build = false;
  bool no_deps = false;

  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--label" && i + 1 < args.size()) {
      label = args[++i];
    } else if (args[i] == "--tag" && i + 1 < args.size()) {
      tag = args[++i];
    } else if (args[i] == "--no-build") {
      no_build = true;
    } else if (args[i] == "--no-deps") {
      no_deps = true;
    } else if (args[i] == "--help" || args[i] == "-h") {
      std::cout << "Usage: nazg workspace snapshot [options]\n\n";
      std::cout << "Create a workspace snapshot\n\n";
      std::cout << "Options:\n";
      std::cout << "  --label <text>   Human-readable label\n";
      std::cout << "  --tag <name>     Named tag for quick reference\n";
      std::cout << "  --no-build       Exclude build directory\n";
      std::cout << "  --no-deps        Don't snapshot dependency manifests\n";
      std::cout << "  --help, -h       Show this help\n";
      return 0;
    }
  }

  // Get current project
  std::string cwd = get_cwd();
  auto project = ctx.store->get_project_by_path(cwd);
  int64_t project_id = 0;
  if (!project) {
    project_id = ctx.store->ensure_project(cwd);
    project = ctx.store->get_project(project_id);
    if (!project) {
      std::cerr << "Error: Could not create project for " << cwd << "\n";
      return 1;
    }
  } else {
    project_id = project->id;
  }

  // Create workspace manager
  Manager mgr(ctx.store, nullptr, ctx.log);

  // Set up snapshot options
  Manager::SnapshotOptions opts;
  opts.label = label.empty() ? "manual snapshot" : label;
  opts.trigger_type = "manual";
  opts.include_build_dir = !no_build;
  opts.include_deps = !no_deps;

  std::cout << "Creating workspace snapshot...\n";

  // Create snapshot
  int64_t snapshot_id = mgr.create_snapshot(project_id, opts);

  if (snapshot_id == 0) {
    std::cerr << "Error: Failed to create snapshot\n";
    return 1;
  }

  std::cout << "✓ Created snapshot #" << snapshot_id << "\n";

  // Tag if requested
  if (!tag.empty()) {
    if (mgr.tag_snapshot(snapshot_id, tag)) {
      std::cout << "✓ Tagged as '" << tag << "'\n";
    } else {
      std::cerr << "Warning: Failed to tag snapshot\n";
    }
  }

  return 0;
}

static int cmd_workspace_history(const directive::command_context &cmd_ctx,
                                  const directive::context &ctx) {
  // Parse arguments from cmd_ctx starting at position 3
  // argv[0] = "nazg", argv[1] = "workspace", argv[2] = "history", argv[3+] = options
  std::vector<std::string> args;
  for (int i = 3; i < cmd_ctx.argc; ++i) {
    args.push_back(cmd_ctx.argv[i]);
  }
  // Parse options
  int limit = 20;
  bool tagged = false;

  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--all") {
      limit = 1000;
    } else if (args[i] == "--tagged") {
      tagged = true;
    } else if (args[i] == "--limit" && i + 1 < args.size()) {
      limit = std::stoi(args[++i]);
    } else if (args[i] == "--help" || args[i] == "-h") {
      std::cout << "Usage: nazg workspace history [options]\n\n";
      std::cout << "List workspace snapshots\n\n";
      std::cout << "Options:\n";
      std::cout << "  --all          Show all snapshots\n";
      std::cout << "  --tagged       Filter by tagged snapshots only\n";
      std::cout << "  --limit <n>    Limit number of snapshots\n";
      std::cout << "  --help, -h     Show this help\n";
      return 0;
    }
  }

  // Get current project
  std::string cwd = get_cwd();
  auto project = ctx.store->get_project_by_path(cwd);
  if (!project) {
    std::cerr << "Error: No workspace snapshots found (not a tracked "
                 "project)\n";
    return 1;
  }

  // Create workspace manager
  Manager mgr(ctx.store, nullptr, ctx.log);

  // List snapshots
  auto snapshots = mgr.list_snapshots(project->id, limit);

  if (snapshots.empty()) {
    std::cout << "No workspace snapshots found\n";
    std::cout << "Create one with: nazg workspace snapshot\n";
    return 0;
  }

  // Get tags
  auto tags = ctx.store->list_workspace_tags(project->id);
  std::map<int64_t, std::string> tag_map;
  for (const auto &tag_row : tags) {
    int64_t snap_id = std::stoll(tag_row.at("snapshot_id"));
    std::string tag_name = tag_row.at("tag_name");
    tag_map[snap_id] = tag_name;
  }

  // Print header
  std::cout << "Workspace Snapshots for " << project->name << "\n\n";
  std::cout << std::left << std::setw(6) << "ID" << std::setw(16) << "Age"
            << std::setw(24) << "Label" << std::setw(12) << "Tag"
            << std::setw(12) << "Trigger"
            << "\n";
  std::cout << std::string(70, '-') << "\n";

  // Print snapshots
  for (const auto &snap : snapshots) {
    // Filter tagged if requested
    if (tagged && tag_map.find(snap.id) == tag_map.end()) {
      continue;
    }

    std::string tag_str =
        tag_map.count(snap.id) ? tag_map[snap.id] : "";

    std::cout << std::left << std::setw(6) << snap.id << std::setw(16)
              << time_ago(snap.timestamp) << std::setw(24) << snap.label
              << std::setw(12) << tag_str << std::setw(12) << snap.trigger_type
              << "\n";
  }

  std::cout << "\nUse 'nazg workspace show <id>' for details\n";
  std::cout << "Use 'nazg workspace restore <id>' to restore\n";

  return 0;
}

static int cmd_workspace_show(const directive::command_context &cmd_ctx,
                               const directive::context &ctx) {
  // Parse arguments from cmd_ctx starting at position 3
  // argv[0] = "nazg", argv[1] = "workspace", argv[2] = "show", argv[3] = snapshot_id
  if (cmd_ctx.argc < 4) {
    std::cout << "Usage: nazg workspace show <snapshot-id>\n";
    std::cout << "   or: nazg workspace show @<tag>\n\n";
    std::cout << "Show detailed information about a snapshot.\n\n";
    std::cout << "Examples:\n";
    std::cout << "  nazg workspace show 1\n";
    std::cout << "  nazg workspace show @working\n";
    return 1;
  }

  std::string snapshot_ref = cmd_ctx.argv[3];

  // Get current project
  std::string cwd = get_cwd();
  auto project = ctx.store->get_project_by_path(cwd);
  if (!project) {
    std::cerr << "Error: No workspace snapshots found (not a tracked project)\n";
    return 1;
  }

  // Create workspace manager
  Manager mgr(ctx.store, nullptr, ctx.log);

  // Parse snapshot reference (ID or @tag)
  std::optional<WorkspaceSnapshot> snapshot;
  if (snapshot_ref[0] == '@') {
    // Tag reference
    std::string tag = snapshot_ref.substr(1);
    snapshot = mgr.get_snapshot_by_tag(project->id, tag);
    if (!snapshot) {
      std::cerr << "Error: No snapshot found with tag '" << tag << "'\n";
      return 1;
    }
  } else {
    // Numeric ID
    try {
      int64_t snapshot_id = std::stoll(snapshot_ref);
      snapshot = mgr.get_snapshot(snapshot_id);
      if (!snapshot) {
        std::cerr << "Error: Snapshot #" << snapshot_id << " not found\n";
        return 1;
      }
    } catch (...) {
      std::cerr << "Error: Invalid snapshot ID: " << snapshot_ref << "\n";
      return 1;
    }
  }

  // Display snapshot details
  std::cout << "Workspace Snapshot #" << snapshot->id << "\n\n";

  if (!snapshot->label.empty()) {
    std::cout << "Label:       " << snapshot->label << "\n";
  }
  if (!snapshot->tags.empty()) {
    std::cout << "Tags:        ";
    for (size_t i = 0; i < snapshot->tags.size(); ++i) {
      if (i > 0) std::cout << ", ";
      std::cout << snapshot->tags[i];
    }
    std::cout << "\n";
  }
  std::cout << "Created:     " << format_timestamp(snapshot->timestamp);
  std::cout << " (" << time_ago(snapshot->timestamp) << ")\n";
  std::cout << "Trigger:     " << snapshot->trigger_type << "\n";

  if (!snapshot->git_branch.empty()) {
    std::cout << "Git:         " << snapshot->git_branch;
    if (!snapshot->git_commit.empty()) {
      std::cout << "@" << snapshot->git_commit.substr(0, 7);
    }
    std::cout << "\n";
  }

  // File statistics
  std::cout << "\nContents:\n";
  int source_count = 0, build_count = 0, dep_count = 0, config_count = 0, other_count = 0;
  int64_t source_size = 0, build_size = 0, dep_size = 0, config_size = 0, other_size = 0;

  for (const auto &file : snapshot->files) {
    switch (file.type) {
    case FileType::SOURCE:
      source_count++;
      source_size += file.size;
      break;
    case FileType::BUILD:
      build_count++;
      build_size += file.size;
      break;
    case FileType::DEPENDENCY:
      dep_count++;
      dep_size += file.size;
      break;
    case FileType::CONFIG:
      config_count++;
      config_size += file.size;
      break;
    default:
      other_count++;
      other_size += file.size;
      break;
    }
  }

  auto format_size = [](int64_t bytes) -> std::string {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes / (1024 * 1024)) + " MB";
  };

  if (source_count > 0) {
    std::cout << "  Source files:       " << std::setw(4) << source_count
              << " files  (" << format_size(source_size) << ")\n";
  }
  if (build_count > 0) {
    std::cout << "  Build artifacts:    " << std::setw(4) << build_count
              << " files  (" << format_size(build_size) << ")\n";
  }
  if (dep_count > 0) {
    std::cout << "  Dependencies:       " << std::setw(4) << dep_count
              << " files  (" << format_size(dep_size) << ")\n";
  }
  if (config_count > 0) {
    std::cout << "  Configuration:      " << std::setw(4) << config_count
              << " files  (" << format_size(config_size) << ")\n";
  }
  if (other_count > 0) {
    std::cout << "  Other:              " << std::setw(4) << other_count
              << " files  (" << format_size(other_size) << ")\n";
  }

  int64_t total_size = source_size + build_size + dep_size + config_size + other_size;
  std::cout << "  Total:              " << std::setw(4) << snapshot->files.size()
            << " files  (" << format_size(total_size) << ")\n";

  // System state (if available)
  if (!snapshot->system_info.empty()) {
    std::cout << "\nSystem State:\n";
    for (const auto &[key, value] : snapshot->system_info) {
      std::cout << "  " << std::left << std::setw(12) << key << ": " << value << "\n";
    }
    std::cout << std::right;
  }

  // Environment (if available)
  if (!snapshot->env_snapshot.empty()) {
    std::cout << "\nEnvironment:\n";
    std::cout << std::left;
    int count = 0;
    for (const auto &[key, raw_value] : snapshot->env_snapshot) {
      std::string value = raw_value;
      if (value.length() > 50) {
        value = value.substr(0, 47) + "...";
      }
      std::cout << "  " << key << "=" << value << "\n";
      if (++count >= 10) {
        if (snapshot->env_snapshot.size() > static_cast<size_t>(count)) {
          std::cout << "  ... (" << snapshot->env_snapshot.size() - count
                    << " more variables)\n";
        }
        break;
      }
    }
    std::cout << std::right;
  }

  // Restore history
  if (snapshot->restore_count > 0) {
    std::cout << "\nRestored:      " << snapshot->restore_count
              << " time" << (snapshot->restore_count > 1 ? "s" : "") << "\n";
  }

  return 0;
}

static int cmd_workspace_diff(const directive::command_context &cmd_ctx,
                               const directive::context &ctx) {
  if (cmd_ctx.argc < 4) {
    std::cout << "Usage: nazg workspace diff <snapshot|@tag> [<snapshot|@tag>]\n";
    std::cout << "       nazg workspace diff <snapshot|@tag> --help\n\n";
    std::cout << "Compare workspace snapshots or compare the current workspace to a snapshot.\n";
    return 0;
  }

  std::string ref_a = cmd_ctx.argv[3];
  if (ref_a == "--help" || ref_a == "-h") {
    std::cout << "Usage: nazg workspace diff <snapshot|@tag> [<snapshot|@tag>]\n\n";
    std::cout << "Examples:\n";
    std::cout << "  nazg workspace diff 42              # current workspace vs snapshot #42\n";
    std::cout << "  nazg workspace diff @stable 55      # compare tagged snapshot to #55\n";
    std::cout << "  nazg workspace diff @stable @beta   # compare two tags\n";
    return 0;
  }

  std::string cwd = get_cwd();
  auto project = ctx.store->get_project_by_path(cwd);
  if (!project) {
    std::cerr << "Error: No workspace found (not a tracked project)\n";
    return 1;
  }

  Manager mgr(ctx.store, nullptr, ctx.log);

  auto resolve_snapshot = [&](const std::string &ref) -> std::optional<int64_t> {
    if (!ref.empty() && ref[0] == '@') {
      auto snap = mgr.get_snapshot_by_tag(project->id, ref.substr(1));
      if (!snap) {
        return std::nullopt;
      }
      return snap->id;
    }
    try {
      return std::stoll(ref);
    } catch (...) {
      return std::nullopt;
    }
  };

  SnapshotDiff diff;
  bool compared_to_current = false;

  if (cmd_ctx.argc >= 5) {
    std::string ref_b = cmd_ctx.argv[4];
    auto id_a = resolve_snapshot(ref_a);
    auto id_b = resolve_snapshot(ref_b);
    if (!id_a || !id_b) {
      std::cerr << "Error: Unable to resolve snapshot reference(s)\n";
      return 1;
    }
    diff = mgr.diff(*id_a, *id_b);
    std::cout << "Diffing snapshot " << ref_a << " (#" << *id_a << ")"
              << " against " << ref_b << " (#" << *id_b << ")\n";
  } else {
    auto id_b = resolve_snapshot(ref_a);
    if (!id_b) {
      std::cerr << "Error: Unable to resolve snapshot reference '" << ref_a << "'\n";
      return 1;
    }
    diff = mgr.diff_current(project->id, *id_b);
    compared_to_current = true;
    std::cout << "Current workspace vs snapshot " << ref_a << " (#" << *id_b << ")\n";
  }

  auto change_type_to_string = [](ChangeType type) -> const char * {
    switch (type) {
    case ChangeType::ADDED:
      return "added";
    case ChangeType::MODIFIED:
      return "modified";
    case ChangeType::DELETED:
      return "deleted";
    default:
      return "unchanged";
    }
  };

  auto print_changes = [&](const char *label, const std::vector<FileChange> &changes) {
    if (changes.empty()) {
      return;
    }
    std::cout << "\n" << label << " (" << changes.size() << "):\n";
    size_t limit = std::min<size_t>(changes.size(), 20);
    for (size_t i = 0; i < limit; ++i) {
      const auto &chg = changes[i];
      std::cout << "  - " << chg.path << " (" << change_type_to_string(chg.change_type)
                << ")\n";
    }
    if (changes.size() > limit) {
      std::cout << "  ... " << (changes.size() - limit) << " more\n";
    }
  };

  print_changes("Source changes", diff.source_changes);
  print_changes("Build changes", diff.build_changes);
  print_changes("Dependency file changes", diff.dep_changes);
  print_changes("Configuration changes", diff.config_changes);
  print_changes("Other changes", diff.other_changes);

  if (!diff.env_changes.empty()) {
    std::cout << "\nEnvironment updates:\n";
    for (const auto &[key, change] : diff.env_changes) {
      std::cout << "  " << key << ": " << change << "\n";
    }
  }

  if (!diff.system_info_changes.empty()) {
    std::cout << "\nSystem info changes:\n";
    for (const auto &[key, change] : diff.system_info_changes) {
      std::cout << "  " << key << ": " << change << "\n";
    }
  }

  if (!diff.dep_upgrades.empty()) {
    std::cout << "\nDependency manifest changes:\n";
    for (const auto &[key, change] : diff.dep_upgrades) {
      std::cout << "  " << key << ": " << change << "\n";
    }
  }

  if (diff.source_changes.empty() && diff.build_changes.empty() &&
      diff.dep_changes.empty() && diff.config_changes.empty() &&
      diff.other_changes.empty() && diff.env_changes.empty() &&
      diff.system_info_changes.empty()) {
    if (compared_to_current) {
      std::cout << "\nWorkspace matches snapshot (no differences).\n";
    } else {
      std::cout << "\nSnapshots are identical (no differences).\n";
    }
  }

  return 0;
}

static int cmd_workspace_restore(const directive::command_context &cmd_ctx,
                                  const directive::context &ctx) {
  if (cmd_ctx.argc < 4) {
    std::cout << "Usage: nazg workspace restore <snapshot-id|@tag> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --full              Restore entire snapshot (default)\n";
    std::cout << "  --smart             Restore non-code changes (configs, deps, etc.)\n";
    std::cout << "  --partial           Restore only specified paths\n";
    std::cout << "  --include <path>    Include path (repeatable)\n";
    std::cout << "  --only <path>       Alias for --include\n";
    std::cout << "  --exclude <path>    Exclude path (repeatable)\n";
    std::cout << "  --dry-run           Show what would be restored\n";
    std::cout << "  --no-interactive    Disable prompts (non-interactive)\n";
    std::cout << "  --help, -h          Show this help\n";
    return 0;
  }

  std::string snapshot_ref = cmd_ctx.argv[3];
  std::vector<std::string> args;
  for (int i = 4; i < cmd_ctx.argc; ++i) {
    args.push_back(cmd_ctx.argv[i]);
  }

  Manager::RestoreOptions opts;
  opts.restore_type = "full";

  for (size_t i = 0; i < args.size(); ++i) {
    const auto &arg = args[i];
    if (arg == "--full") {
      opts.restore_type = "full";
    } else if (arg == "--smart") {
      opts.restore_type = "smart";
    } else if (arg == "--partial") {
      opts.restore_type = "partial";
    } else if (arg == "--dry-run") {
      opts.dry_run = true;
    } else if (arg == "--no-interactive") {
      opts.interactive = false;
    } else if ((arg == "--include" || arg == "--only") && i + 1 < args.size()) {
      opts.include_paths.push_back(args[++i]);
    } else if (arg == "--exclude" && i + 1 < args.size()) {
      opts.exclude_paths.push_back(args[++i]);
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg workspace restore <snapshot-id|@tag> [options]\n";
      return 0;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      return 1;
    }
  }

  if (!opts.include_paths.empty() && opts.restore_type == "full") {
    opts.restore_type = "partial";
  }

  // Get project
  std::string cwd = get_cwd();
  auto project = ctx.store->get_project_by_path(cwd);
  if (!project) {
    std::cerr << "Error: No workspace found (not a tracked project)\n";
    return 1;
  }

  Manager mgr(ctx.store, nullptr, ctx.log);

  int64_t snapshot_id = 0;
  if (!snapshot_ref.empty() && snapshot_ref[0] == '@') {
    auto snapshot = mgr.get_snapshot_by_tag(project->id, snapshot_ref.substr(1));
    if (!snapshot) {
      std::cerr << "Error: No snapshot found with tag '" << snapshot_ref.substr(1)
                << "'\n";
      return 1;
    }
    snapshot_id = snapshot->id;
  } else {
    try {
      snapshot_id = std::stoll(snapshot_ref);
    } catch (...) {
      std::cerr << "Error: Invalid snapshot reference '" << snapshot_ref << "'\n";
      return 1;
    }
  }

  auto snapshot = mgr.get_snapshot(snapshot_id);
  if (!snapshot) {
    std::cerr << "Error: Snapshot #" << snapshot_id << " not found\n";
    return 1;
  }

  if (opts.dry_run) {
    std::cout << "Previewing restore for snapshot #" << snapshot_id << "\n";
  } else {
    std::cout << "Restoring workspace to snapshot #" << snapshot_id << "...\n";
  }

  auto result = mgr.restore(project->id, snapshot_id, opts);

  if (!result.errors.empty()) {
    for (const auto &err : result.errors) {
      std::cerr << "  " << err << "\n";
    }
  }

  if (opts.dry_run) {
    std::cout << "Would restore " << result.files_restored << " file(s)";
    int filtered = result.files_skipped - result.files_restored;
    if (filtered > 0) {
      std::cout << " (" << filtered << " filtered)";
    }
    std::cout << "\n";
    return result.errors.empty() ? 0 : 1;
  }

  if (result.success) {
    std::cout << "✓ Restored " << result.files_restored << " file(s)";
    if (result.files_skipped > 0) {
      std::cout << " (" << result.files_skipped << " skipped)";
    }
    std::cout << "\n";
    std::cout << "Duration: " << result.duration_ms << " ms\n";
    return 0;
  }

  std::cerr << "Restore encountered errors\n";
  return 1;
}

static int cmd_workspace_tag(const directive::command_context &/*cmd_ctx*/,
                              const directive::context &/*ctx*/) {
  std::cout << "nazg workspace tag - not implemented yet\n";
  return 1;
}

static int cmd_workspace_prune(const directive::command_context &cmd_ctx,
                                const directive::context &ctx) {
  // Parse arguments from cmd_ctx starting at position 3
  std::vector<std::string> args;
  for (int i = 3; i < cmd_ctx.argc; ++i) {
    args.push_back(cmd_ctx.argv[i]);
  }

  // Parse options
  int keep_count = -1;
  int days_old = -1;
  bool untagged_only = false;
  bool dry_run = false;

  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--keep" && i + 1 < args.size()) {
      keep_count = std::stoi(args[++i]);
    } else if (args[i] == "--days" && i + 1 < args.size()) {
      days_old = std::stoi(args[++i]);
    } else if (args[i] == "--untagged-only") {
      untagged_only = true;
    } else if (args[i] == "--dry-run") {
      dry_run = true;
    } else if (args[i] == "--help" || args[i] == "-h") {
      std::cout << "Usage: nazg workspace prune [options]\n\n";
      std::cout << "Clean up old workspace snapshots\n\n";
      std::cout << "Options:\n";
      std::cout << "  --keep <n>         Keep only the N most recent snapshots\n";
      std::cout << "  --days <n>         Delete untagged snapshots older than N days\n";
      std::cout << "  --untagged-only    Only prune untagged snapshots\n";
      std::cout << "  --dry-run          Show what would be deleted without deleting\n";
      std::cout << "  --help, -h         Show this help\n\n";
      std::cout << "Examples:\n";
      std::cout << "  nazg workspace prune --keep 10\n";
      std::cout << "  nazg workspace prune --days 30 --untagged-only\n";
      std::cout << "  nazg workspace prune --keep 5 --dry-run\n";
      return 0;
    }
  }

  if (keep_count == -1 && days_old == -1) {
    std::cerr << "Error: Must specify either --keep or --days\n";
    std::cerr << "Run 'nazg workspace prune --help' for usage\n";
    return 1;
  }

  // Get current project
  std::string cwd = get_cwd();
  auto project = ctx.store->get_project_by_path(cwd);
  if (!project) {
    std::cerr << "Error: No workspace found (not a tracked project)\n";
    return 1;
  }

  // Create workspace manager
  Manager mgr(ctx.store, nullptr, ctx.log);

  Manager::PruneOptions prune_opts;
  prune_opts.dry_run = dry_run;
  prune_opts.untagged_only = untagged_only;

  Manager::PruneResult prune_result;

  if (keep_count > 0) {
    std::cout << "Pruning to keep " << keep_count << " most recent snapshots";
    if (untagged_only) {
      std::cout << " (untagged only)";
    }
    if (dry_run) {
      std::cout << " (dry run)";
    }
    std::cout << "...\n";

    prune_result =
        mgr.prune_old_snapshots(project->id, keep_count, prune_opts);
  } else if (days_old > 0) {
    std::cout << "Pruning snapshots older than " << days_old << " days";
    if (untagged_only) {
      std::cout << " (untagged only)";
    }
    if (dry_run) {
      std::cout << " (dry run)";
    }
    std::cout << "...\n";

    prune_result =
        mgr.prune_older_than(project->id, days_old, prune_opts);
  }

  if (prune_opts.dry_run) {
    if (prune_result.deleted_ids.empty()) {
      std::cout << "No snapshots would be removed.\n";
    } else {
      std::cout << "Would remove " << prune_result.deleted_ids.size()
                << " snapshot(s): ";
      for (size_t i = 0; i < prune_result.deleted_ids.size(); ++i) {
        if (i > 0)
          std::cout << ", ";
        std::cout << "#" << prune_result.deleted_ids[i];
      }
      std::cout << "\n";
    }
  } else {
    if (prune_result.deleted_ids.empty()) {
      std::cout << "No snapshots were removed.\n";
    } else {
      std::cout << "✓ Removed " << prune_result.deleted_ids.size()
                << " snapshot(s): ";
      for (size_t i = 0; i < prune_result.deleted_ids.size(); ++i) {
        if (i > 0)
          std::cout << ", ";
        std::cout << "#" << prune_result.deleted_ids[i];
      }
      std::cout << "\n";
    }
  }

  if (prune_result.skipped_tagged > 0) {
    std::cout << "Skipped " << prune_result.skipped_tagged
              << " tagged snapshot(s)";
    if (!prune_result.skipped_tagged_ids.empty()) {
      std::cout << " (";
      for (size_t i = 0; i < prune_result.skipped_tagged_ids.size(); ++i) {
        if (i > 0)
          std::cout << ", ";
        std::cout << "#" << prune_result.skipped_tagged_ids[i];
      }
      std::cout << ")";
    }
    std::cout << "\n";
  }

  return 0;
}

// Root workspace command dispatcher
static int cmd_workspace_root(const directive::command_context &cmd_ctx,
                               const directive::context &ctx) {
  // argv[0] = "nazg", argv[1] = "workspace", argv[2] = subcommand
  if (cmd_ctx.argc < 3) {
    std::cout << "Usage: nazg workspace <command> [options]\n\n";
    std::cout << "Workspace Time Machine - Intelligent workspace state "
                 "management\n\n";
    std::cout << "Commands:\n";
    std::cout << "  snapshot      Create a workspace snapshot\n";
    std::cout << "  history       List workspace snapshots\n";
    std::cout << "  show          Show detailed snapshot information\n";
    std::cout << "  diff          Compare snapshots or current state\n";
    std::cout << "  restore       Restore workspace to a snapshot\n";
    std::cout << "  tag           Manage snapshot tags\n";
    std::cout << "  prune         Clean up old snapshots\n\n";
    std::cout << "Use 'nazg workspace <command> --help' for command-specific "
                 "help\n";
    return 0;
  }

  std::string subcmd = cmd_ctx.argv[2];

  // Create sub-context with args starting from position 3
  directive::command_context sub_ctx;
  sub_ctx.argc = cmd_ctx.argc;
  sub_ctx.argv = cmd_ctx.argv;  // Pass full argv so subcommands can access all context

  if (subcmd == "snapshot") {
    return cmd_workspace_snapshot(sub_ctx, ctx);
  } else if (subcmd == "history") {
    return cmd_workspace_history(sub_ctx, ctx);
  } else if (subcmd == "show") {
    return cmd_workspace_show(sub_ctx, ctx);
  } else if (subcmd == "diff") {
    return cmd_workspace_diff(sub_ctx, ctx);
  } else if (subcmd == "restore") {
    return cmd_workspace_restore(sub_ctx, ctx);
  } else if (subcmd == "tag") {
    return cmd_workspace_tag(sub_ctx, ctx);
  } else if (subcmd == "prune") {
    return cmd_workspace_prune(sub_ctx, ctx);
  } else {
    std::cerr << "Error: Unknown workspace command: " << subcmd << "\n";
    std::cerr << "Run 'nazg workspace' for usage\n";
    return 1;
  }
}

void register_commands(nazg::directive::registry &reg,
                       nazg::directive::context &/*ctx*/) {
  reg.add("workspace", "Workspace state management (time machine)",
          cmd_workspace_root);
}

} // namespace nazg::workspace

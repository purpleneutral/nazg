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

#include "test/workspace_suite.hpp"

#include "blackbox/logger.hpp"
#include "blackbox/options.hpp"
#include "test/runner.hpp"
#include "workspace/manager.hpp"

#include <cstdlib>
#include <functional>

namespace nazg::test::workspace_suite {

namespace {

struct ContextGuard {
  nazg::test::Runner &runner;
  nazg::test::Runner::WorkspaceTestContext &ctx;
  ~ContextGuard() { runner.teardown_workspace_test(ctx); }
};

bool verify(bool condition, const std::string &message, std::string &error) {
  if (!condition && error.empty()) {
    error = message;
  }
  return condition;
}

} // namespace

bool run_snapshot_creation(nazg::blackbox::logger *log, std::string &error) {
  error.clear();

  nazg::test::Runner runner(nullptr, log);
  auto ctx = runner.setup_workspace_test("snapshot_creation");
  ContextGuard guard{runner, ctx};

  ctx.create_file("src/main.cpp", "int main() { return 0; }\n");
  ctx.create_file("README.md", "# Workspace Test\n");

  int64_t snapshot_id = ctx.create_snapshot("initial");
  if (!verify(snapshot_id > 0, "Failed to create snapshot", error)) {
    return false;
  }

  ctx.verify_snapshot_exists(snapshot_id);
  auto snapshot = ctx.ws_mgr->get_snapshot(snapshot_id);
  if (!verify(snapshot.has_value(), "Snapshot lookup failed", error)) {
    return false;
  }
  if (!verify(snapshot->label == "initial", "Snapshot label mismatch", error)) {
    return false;
  }

  bool has_main = false;
  for (const auto &file : snapshot->files) {
    if (file.path == "src/main.cpp") {
      has_main = true;
      break;
    }
  }
  if (!verify(has_main, "Snapshot missing src/main.cpp", error)) {
    return false;
  }

  if (!ctx.ws_mgr->tag_snapshot(snapshot_id, "baseline")) {
    error = "Failed to tag snapshot";
    return false;
  }

  auto tagged = ctx.ws_mgr->get_snapshot(snapshot_id);
  if (!verify(tagged.has_value(), "Tagged snapshot lookup failed", error)) {
    return false;
  }

  bool tag_found = false;
  for (const auto &tag : tagged->tags) {
    if (tag == "baseline") {
      tag_found = true;
      break;
    }
  }
  return verify(tag_found, "Snapshot tag not recorded", error);
}

bool run_prune_behavior(nazg::blackbox::logger *log, std::string &error) {
  error.clear();

  nazg::test::Runner runner(nullptr, log);
  auto ctx = runner.setup_workspace_test("prune_behavior");
  ContextGuard guard{runner, ctx};

  ctx.create_file("src/app.cpp", "int value() { return 1; }\n");
  ctx.create_snapshot("first");

  ctx.modify_file("src/app.cpp", "int value() { return 2; }\n");
  int64_t second = ctx.create_snapshot("second");
  if (!ctx.ws_mgr->tag_snapshot(second, "stable")) {
    error = "Failed to tag second snapshot";
    return false;
  }

  ctx.modify_file("src/app.cpp", "int value() { return 3; }\n");
  ctx.create_snapshot("third");

  auto before = ctx.ws_mgr->list_snapshots(ctx.project_id, 10);
  if (!verify(before.size() >= 3, "Expected at least three snapshots", error)) {
    return false;
  }

  nazg::workspace::Manager::PruneOptions opts;
  opts.dry_run = true;
  opts.untagged_only = true;

  auto dry_run = ctx.ws_mgr->prune_old_snapshots(ctx.project_id, 1, opts);
  if (!verify(dry_run.deleted >= 1, "Dry-run expected to delete at least one snapshot", error)) {
    return false;
  }
  if (!verify(dry_run.skipped_tagged == 1, "Dry-run should report tagged snapshot skip", error)) {
    return false;
  }

  opts.dry_run = false;
  auto prune = ctx.ws_mgr->prune_old_snapshots(ctx.project_id, 1, opts);
  if (!verify(prune.skipped_tagged == 1, "Live prune should skip tagged snapshot", error)) {
    return false;
  }

  auto remaining = ctx.ws_mgr->list_snapshots(ctx.project_id, 10);
  if (!verify(!remaining.empty(), "No snapshots remain after prune", error)) {
    return false;
  }
  if (!verify(static_cast<int>(remaining.size()) ==
                  static_cast<int>(before.size()) - prune.deleted,
              "Snapshot count mismatch after prune", error)) {
    return false;
  }

  if (!verify(ctx.ws_mgr->get_snapshot(second).has_value(),
              "Tagged snapshot removed unexpectedly", error)) {
    return false;
  }

  return true;
}

bool run_env_capture(nazg::blackbox::logger *log, std::string &error) {
  error.clear();

  const char *previous_cc = std::getenv("CC");
  std::string restore_value = previous_cc ? previous_cc : "";

#ifdef _WIN32
  _putenv_s("CC", "nazg-test-cc");
#else
  setenv("CC", "nazg-test-cc", 1);
#endif

  nazg::test::Runner runner(nullptr, log);
  auto ctx = runner.setup_workspace_test("env_capture");
  ContextGuard guard{runner, ctx};

  ctx.create_file("src/env.cpp", "int main() { return 0; }\n");
  int64_t snapshot_id = ctx.create_snapshot("env");
  auto snapshot = ctx.ws_mgr->get_snapshot(snapshot_id);
  bool success = true;
  if (!verify(snapshot.has_value(), "Snapshot lookup failed", error)) {
    success = false;
  } else {
    auto it = snapshot->env_snapshot.find("CC");
    success = verify(it != snapshot->env_snapshot.end(), "CC variable missing from snapshot", error) &&
              verify(it->second == "nazg-test-cc", "Captured CC value mismatch", error);
  }

  if (previous_cc) {
#ifdef _WIN32
    _putenv_s("CC", restore_value.c_str());
#else
    setenv("CC", restore_value.c_str(), 1);
#endif
  } else {
#ifdef _WIN32
    _putenv_s("CC", "");
#else
    unsetenv("CC");
#endif
  }

  return success;
}

bool run_restore_full(nazg::blackbox::logger *log, std::string &error) {
  error.clear();

  nazg::test::Runner runner(nullptr, log);
  auto ctx = runner.setup_workspace_test("restore_full");
  ContextGuard guard{runner, ctx};

  const std::string original = "int value() { return 1; }\n";
  const std::string modified = "int value() { return 42; }\n";

  ctx.create_file("src/value.cpp", original);
  int64_t snapshot_id = ctx.create_snapshot("baseline");
  ctx.verify_snapshot_exists(snapshot_id);

  ctx.modify_file("src/value.cpp", modified);
  ctx.verify_file_content("src/value.cpp", modified);

  nazg::workspace::Manager::RestoreOptions dry_opts;
  dry_opts.restore_type = "full";
  dry_opts.dry_run = true;
  dry_opts.interactive = false;

  auto preview = ctx.ws_mgr->restore(ctx.project_id, snapshot_id, dry_opts);
  if (!verify(preview.success, "Dry-run restore failed", error)) {
    return false;
  }
  ctx.verify_file_content("src/value.cpp", modified);

  nazg::workspace::Manager::RestoreOptions run_opts;
  run_opts.restore_type = "full";
  run_opts.dry_run = false;
  run_opts.interactive = false;

  auto result = ctx.ws_mgr->restore(ctx.project_id, snapshot_id, run_opts);
  if (!verify(result.success, "Full restore reported failure", error)) {
    return false;
  }
  if (!verify(result.files_restored >= 1, "No files restored", error)) {
    return false;
  }

  ctx.verify_file_content("src/value.cpp", original);
  return true;
}

} // namespace nazg::test::workspace_suite

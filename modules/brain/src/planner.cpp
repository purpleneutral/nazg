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

#include "brain/planner.hpp"
#include "brain/detector.hpp"
#include "brain/snapshot.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"

#include <sstream>

namespace nazg::brain {

Planner::Planner(nazg::nexus::Store *store, nazg::blackbox::logger *log)
    : store_(store), log_(log) {}

Plan Planner::decide(int64_t project_id, const ProjectInfo &info,
                     const SnapshotResult &snapshot) {
  Plan plan;

  // If snapshot unchanged, decide based on test availability
  if (!snapshot.changed && !snapshot.previous_hash.empty()) {
    // If has tests, suggest running them (even without changes)
    if (info.has_tests) {
      plan = generate_test_plan(info);
      plan.reason = "No changes detected, but tests are available";

      if (log_) {
        log_->info("Brain", "Decision: TEST - " + plan.reason);
      }

      if (store_) {
        store_->add_event(project_id, "info", "planner",
                         "Decision: TEST (no changes)", plan.reason);
      }

      return plan;
    }

    // No changes and no tests - skip
    plan.action = Action::SKIP;
    plan.reason = "No changes detected (tree hash: " + snapshot.tree_hash.substr(0, 8) + ")";

    if (log_) {
      log_->info("Brain", "Decision: SKIP - " + plan.reason);
    }

    if (store_) {
      store_->add_event(project_id, "info", "planner",
                       "Decision: SKIP (no changes)", "");
    }

    return plan;
  }

  // Changes detected or first build
  // If has build system, suggest build (tests can be run after)
  if (info.build_system != "unknown") {
    plan = generate_build_plan(info);
    plan.reason = snapshot.changed
                      ? "Files changed (previous: " + snapshot.previous_hash.substr(0, 8) +
                            ", current: " + snapshot.tree_hash.substr(0, 8) + ")"
                      : "First build";

    // If has tests, mark that tests should run after build
    if (info.has_tests) {
      plan.run_after_build = true;
    }

    if (log_) {
      log_->info("Brain", "Decision: BUILD - " + plan.reason);
      log_->info("Brain", "Command: " + plan.command);
      if (plan.run_after_build) {
        log_->info("Brain", "Will run tests after successful build");
      }
    }

    if (store_) {
      store_->add_event(project_id, "info", "planner",
                       "Decision: BUILD", plan.reason);
    }

    return plan;
  }

  // No build system but has tests - run tests directly
  if (info.has_tests) {
    plan = generate_test_plan(info);
    plan.reason = snapshot.changed ? "Files changed, running tests" : "First test run";

    if (log_) {
      log_->info("Brain", "Decision: TEST - " + plan.reason);
    }

    if (store_) {
      store_->add_event(project_id, "info", "planner",
                       "Decision: TEST", plan.reason);
    }

    return plan;
  }

  // No build system and no tests - unknown
  plan.action = Action::UNKNOWN;
  plan.reason = "No build system or tests detected";

  if (log_) {
    log_->warn("Brain", "Decision: UNKNOWN - " + plan.reason);
  }

  if (store_) {
    store_->add_event(project_id, "warn", "planner",
                     "Decision: UNKNOWN", plan.reason);
  }

  return plan;
}

Plan Planner::generate_build_plan(const ProjectInfo &info) {
  Plan plan;
  plan.action = Action::BUILD;
  plan.working_dir = info.root_path;

  if (info.build_system == "cmake") {
    // Use shell to chain configure + build
    plan.command = "/bin/sh";
    plan.args = {"-c", "cmake -B build -S . && cmake --build build -j4"};
  } else if (info.build_system == "make") {
    plan.command = "make";
    plan.args = {"-j4"};
  } else if (info.build_system == "cargo") {
    plan.command = "cargo";
    plan.args = {"build", "--release"};
  } else if (info.build_system == "npm") {
    plan.command = "npm";
    plan.args = {"run", "build"};
  } else {
    plan.action = Action::UNKNOWN;
    plan.reason = "Unknown build system: " + info.build_system;
  }

  return plan;
}

Plan Planner::generate_test_plan(const ProjectInfo &info) {
  Plan plan;
  plan.action = Action::TEST;
  plan.working_dir = info.root_path;
  plan.test_framework = info.test_framework;

  if (info.test_framework == "gtest" || info.test_framework == "ctest") {
    plan.command = "ctest";
    plan.args = {"--output-on-failure", "--test-dir", "build"};
  } else if (info.test_framework == "pytest") {
    plan.command = "pytest";
    plan.args = {"-v", "--tb=short"};
  } else if (info.test_framework == "cargo") {
    plan.command = "cargo";
    plan.args = {"test", "--", "--nocapture"};
  } else if (info.test_framework == "jest") {
    plan.command = "npm";
    plan.args = {"test", "--", "--verbose"};
  } else if (info.test_framework == "vitest") {
    plan.command = "npm";
    plan.args = {"run", "test"};
  } else if (info.test_framework == "go") {
    plan.command = "go";
    plan.args = {"test", "-v", "./..."};
  } else if (info.test_framework == "catch2") {
    // Catch2 tests are typically built executables
    plan.command = "./build/tests";
    plan.args = {};
  } else {
    plan.action = Action::UNKNOWN;
    plan.reason = "Unknown test framework: " + info.test_framework;
  }

  return plan;
}

} // namespace nazg::brain

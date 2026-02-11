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

#include "brain/commands.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"

#include "brain/detector.hpp"
#include "brain/learner.hpp"
#include "brain/pattern_matcher.hpp"
#include "brain/planner.hpp"
#include "brain/recovery.hpp"
#include "brain/snapshot.hpp"
#include "task/builder.hpp"

#include "nexus/store.hpp"
#include "nexus/config.hpp"
#include "blackbox/logger.hpp"
#include "system/fs.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace nazg::brain {

// Get current working directory
static std::string get_cwd() {
  char buf[4096];
  if (::getcwd(buf, sizeof(buf))) {
    return std::string(buf);
  }
  return ".";
}

// nazg build - smart build with intelligence
static int cmd_build(const directive::command_context &/*ctx*/,
                     const directive::context &ectx) {

  if (!ectx.store) {
    if (ectx.log) {
      ectx.log->error("Engine", "Database not initialized");
    }
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  std::string root_path = get_cwd();

  // Ensure project exists
  int64_t project_id = ectx.store->ensure_project(root_path);

  // 1. Detect project
  brain::Detector detector(ectx.store, ectx.log);
  auto info = detector.detect(root_path);
  detector.store_facts(project_id, info);

  // 2. Compute snapshot
  brain::Snapshotter snapshotter(ectx.store, ectx.log);
  auto snapshot = snapshotter.compute(project_id, root_path);
  snapshotter.store_snapshot(project_id, snapshot);

  // 3. Plan action
  brain::Planner planner(ectx.store, ectx.log);
  auto plan = planner.decide(project_id, info, snapshot);

  // 4. Execute (if needed)
  task::Builder builder(ectx.store, ectx.log);
  auto result = builder.build(project_id, plan);
  builder.record_build(project_id, plan, result);

  // 5. Show results
  if (plan.action == brain::Action::SKIP) {
    std::cout << "⏩ SKIP: " << plan.reason << "\n";
    return 0;
  } else if (plan.action == brain::Action::BUILD) {
    if (result.success) {
      std::cout << "✓ BUILD SUCCESS (" << result.duration_ms << "ms)\n";
      std::cout << "  " << plan.command;
      for (const auto &arg : plan.args) std::cout << " " << arg;
      std::cout << "\n";
      return 0;
    } else {
      std::cout << "✗ BUILD FAILED (exit: " << result.exit_code << ")\n";
      return result.exit_code;
    }
  } else {
    std::cout << "? UNKNOWN: " << plan.reason << "\n";
    return 1;
  }
}

// nazg build status - show project status
static int cmd_build_status(const directive::command_context &/*ctx*/,
                            const directive::context &ectx) {

  if (!ectx.store) {
    if (ectx.log) {
      ectx.log->error("Engine", "Database not initialized");
    }
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  std::string root_path = get_cwd();
  auto project = ectx.store->get_project_by_path(root_path);

  if (!project) {
    std::cout << "Project: " << root_path << "\n";
    std::cout << "Status: Not detected yet (run 'nazg build' first)\n";
    return 0;
  }

  // Get facts
  auto facts = ectx.store->list_facts(project->id, "detector");

  std::cout << "Project: " << root_path << "\n";
  std::cout << "Language: " << facts["language"] << "\n";
  std::cout << "Build System: " << facts["build_system"] << "\n";
  std::cout << "SCM: " << facts["scm"] << "\n";

  // Get latest snapshot
  auto snapshot = ectx.store->latest_snapshot(project->id);
  if (snapshot) {
    std::cout << "Files: " << snapshot->file_count
              << " (" << (snapshot->total_bytes / 1024) << " KB)\n";
    std::cout << "Snapshot: " << snapshot->tree_hash.substr(0, 16) << "...\n";
  }

  return 0;
}

// nazg build facts - list all facts
static int cmd_build_facts(const directive::command_context &/*ctx*/,
                           const directive::context &ectx) {

  if (!ectx.store) {
    if (ectx.log) {
      ectx.log->error("Engine", "Database not initialized");
    }
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  std::string root_path = get_cwd();
  auto project = ectx.store->get_project_by_path(root_path);

  if (!project) {
    std::cout << "No facts found (run 'nazg build' first)\n";
    return 0;
  }

  // Get all detector facts
  auto facts = ectx.store->list_facts(project->id, "detector");

  std::cout << "Facts for: " << root_path << "\n\n";
  for (const auto &[key, value] : facts) {
    std::cout << "detector." << key << " = " << value << "\n";
  }

  return 0;
}

// nazg why - explain what would happen without executing
static int cmd_why(const directive::command_context &/*ctx*/,
                   const directive::context &ectx) {

  if (!ectx.store) {
    if (ectx.log) {
      ectx.log->error("Engine", "Database not initialized");
    }
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  std::string root_path = get_cwd();

  // Ensure project exists
  int64_t project_id = ectx.store->ensure_project(root_path);

  std::cout << "🧠 Analyzing project...\n\n";

  // 1. Detect project
  brain::Detector detector(ectx.store, ectx.log);
  auto info = detector.detect(root_path);
  detector.store_facts(project_id, info);

  std::cout << "📁 Project: " << root_path << "\n";
  std::cout << "   Language: " << info.language << "\n";
  std::cout << "   Build System: " << info.build_system << "\n";
  std::cout << "   SCM: " << info.scm << "\n";

  // 2. Compute snapshot
  brain::Snapshotter snapshotter(ectx.store, ectx.log);
  auto snapshot = snapshotter.compute(project_id, root_path);

  std::cout << "\n📊 Snapshot:\n";
  std::cout << "   Files: " << snapshot.file_count << "\n";
  std::cout << "   Size: " << (snapshot.total_bytes / 1024) << " KB\n";
  std::cout << "   Hash: " << snapshot.tree_hash.substr(0, 16) << "...\n";

  if (snapshot.changed) {
    std::cout << "   Status: CHANGED (prev: "
              << snapshot.previous_hash.substr(0, 8) << "...)\n";
  } else {
    std::cout << "   Status: UNCHANGED\n";
  }

  // 3. Plan action
  brain::Planner planner(ectx.store, ectx.log);
  auto plan = planner.decide(project_id, info, snapshot);

  std::cout << "\n🎯 Decision:\n";
  if (plan.action == brain::Action::SKIP) {
    std::cout << "   Action: ⏩ SKIP\n";
    std::cout << "   Reason: " << plan.reason << "\n";
  } else if (plan.action == brain::Action::BUILD) {
    std::cout << "   Action: 🔨 BUILD\n";
    std::cout << "   Reason: " << plan.reason << "\n";
    std::cout << "   Command: " << plan.command;
    for (const auto &arg : plan.args) std::cout << " " << arg;
    std::cout << "\n";
    std::cout << "   Working Dir: " << plan.working_dir << "\n";
  } else if (plan.action == brain::Action::CLEAN) {
    std::cout << "   Action: 🧹 CLEAN + BUILD\n";
    std::cout << "   Reason: " << plan.reason << "\n";
  } else {
    std::cout << "   Action: ❓ UNKNOWN\n";
    std::cout << "   Reason: " << plan.reason << "\n";
  }

  std::cout << "\n💡 Run 'nazg build' to execute this plan\n";

  return 0;
}

// nazg brain failures - list recent failures
static int cmd_brain_failures(const directive::command_context &ctx,
                               const directive::context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  std::string root_path = get_cwd();
  int64_t project_id = ectx.store->ensure_project(root_path);

  brain::Learner learner(ectx.store, nullptr, ectx.log);

  // Check for --unresolved flag
  bool unresolved_only = false;
  for (int i = 0; i < ctx.argc; ++i) {
    if (std::string(ctx.argv[i]) == "--unresolved") {
      unresolved_only = true;
    }
  }

  auto failures = unresolved_only
                      ? learner.list_unresolved_failures(project_id, 50)
                      : learner.list_failures(project_id, 50);

  if (failures.empty()) {
    std::cout << "No failures recorded for this project.\n";
    return 0;
  }

  // Get stats
  auto stats = learner.get_statistics(project_id);

  std::cout << "\n";
  if (unresolved_only) {
    std::cout << "Recent Unresolved Failures for " << root_path << "\n\n";
  } else {
    std::cout << "Recent Failures for " << root_path << "\n\n";
  }

  std::cout << "ID    Age       Type    Status      Error\n";
  std::cout << "──────────────────────────────────────────────────────────────\n";

  auto now = std::chrono::system_clock::now();
  auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch())
                     .count();

  for (const auto &failure : failures) {
    int64_t id = std::stoll(failure.at("id"));
    int64_t timestamp = std::stoll(failure.at("timestamp"));
    std::string type = failure.at("failure_type");
    std::string error_msg = failure.at("error_message");
    bool resolved = failure.at("resolved") == "1";

    // Calculate age
    int64_t age_sec = now_sec - timestamp;
    std::string age_str;
    if (age_sec < 60) {
      age_str = std::to_string(age_sec) + "s ago";
    } else if (age_sec < 3600) {
      age_str = std::to_string(age_sec / 60) + "m ago";
    } else if (age_sec < 86400) {
      age_str = std::to_string(age_sec / 3600) + "h ago";
    } else {
      age_str = std::to_string(age_sec / 86400) + "d ago";
    }

    // Truncate error message for display
    std::string error_summary = error_msg.substr(0, 50);
    if (error_msg.length() > 50) {
      error_summary += "...";
    }

    // Remove newlines
    for (auto &c : error_summary) {
      if (c == '\n')
        c = ' ';
    }

    printf("%-5lld %-9s %-7s %-11s %s\n", (long long)id, age_str.c_str(),
           type.c_str(), resolved ? "resolved" : "unresolved",
           error_summary.c_str());
  }

  std::cout << "\n";
  std::cout << failures.size() << " failure(s) shown";
  if (!unresolved_only) {
    std::cout << " (" << stats.unresolved_failures << " unresolved)";
  }
  std::cout << "\n";

  std::cout << "\nUse 'nazg brain failure <id>' for details\n";

  return 0;
}

// nazg brain failure <id> - show failure details
static int cmd_brain_failure(const directive::command_context &ctx,
                              const directive::context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  if (ctx.argc < 1) {
    std::cerr << "Error: Missing failure ID\n";
    std::cerr << "Usage: nazg brain failure <id>\n";
    return 1;
  }

  int64_t failure_id = std::stoll(ctx.argv[0]);

  brain::Learner learner(ectx.store, nullptr, ectx.log);
  auto failure_opt = learner.get_failure(failure_id);

  if (!failure_opt) {
    std::cerr << "Error: Failure #" << failure_id << " not found\n";
    return 1;
  }

  const auto &failure = *failure_opt;

  // Display failure details
  std::cout << "\n";
  std::cout << "Failure #" << failure_id << " ("
            << failure.at("failure_type") << " Failure)\n\n";

  // Timestamp
  int64_t timestamp = std::stoll(failure.at("timestamp"));
  auto tp = std::chrono::system_clock::from_time_t(timestamp);
  std::time_t tt = std::chrono::system_clock::to_time_t(tp);
  std::cout << "Occurred:    " << std::ctime(&tt);

  // Status
  bool resolved = failure.at("resolved") == "1";
  std::cout << "Status:      " << (resolved ? "Resolved" : "Unresolved")
            << "\n";
  if (resolved) {
    std::cout << "Resolution:  " << failure.at("resolution_type") << "\n";
  }

  // Command and exit code
  if (!failure.at("command_executed").empty()) {
    std::cout << "Command:     " << failure.at("command_executed") << "\n";
  }
  int exit_code = std::stoi(failure.at("exit_code"));
  if (exit_code != 0) {
    std::cout << "Exit Code:   " << exit_code << "\n";
  }

  std::cout << "Severity:    " << failure.at("severity") << "\n";

  // Error location
  if (!failure.at("error_location").empty()) {
    std::cout << "Location:    " << failure.at("error_location") << "\n";
  }

  std::cout << "\n";

  // Error message
  std::cout << "Error:\n";
  std::string error_msg = failure.at("error_message");
  std::istringstream iss(error_msg);
  std::string line;
  int line_count = 0;
  while (std::getline(iss, line) && line_count < 20) {
    std::cout << "  " << line << "\n";
    line_count++;
  }
  if (line_count == 20) {
    std::cout << "  ... (truncated)\n";
  }

  std::cout << "\n";

  // Snapshots
  int64_t before_snap = std::stoll(failure.at("before_snapshot_id"));
  int64_t after_snap = std::stoll(failure.at("after_snapshot_id"));
  if (before_snap > 0 || after_snap > 0) {
    std::cout << "Snapshots:\n";
    if (before_snap > 0) {
      std::cout << "  Before:  #" << before_snap << " (last known good)\n";
    }
    if (after_snap > 0) {
      std::cout << "  After:   #" << after_snap << " (failed state)\n";
    }
    std::cout << "\n";
  }

  // Similar failures
  auto similar = learner.find_similar_failures(
      std::stoll(failure.at("project_id")), failure.at("error_signature"), 3);
  if (similar.size() > 1) { // More than just this failure
    std::cout << "Similar Failures:\n";
    for (const auto &sim : similar) {
      if (sim.at("id") == failure.at("id"))
        continue; // Skip self
      std::cout << "  • Failure #" << sim.at("id");
      int64_t sim_ts = std::stoll(sim.at("timestamp"));
      int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
      int64_t age = now_sec - sim_ts;
      if (age < 3600 * 24) {
        std::cout << " (" << (age / 3600) << "h ago)";
      } else {
        std::cout << " (" << (age / 86400) << "d ago)";
      }
      if (sim.at("resolved") == "1") {
        std::cout << " - resolved via " << sim.at("resolution_type");
      }
      std::cout << "\n";
    }
    std::cout << "\n";
  }

  if (!resolved) {
    std::cout << "💡 Tip: This feature will suggest recovery actions in a "
                 "future update\n";
  }

  return 0;
}

// nazg brain patterns - list learned patterns
static int cmd_brain_patterns(const directive::command_context &/*ctx*/,
                               const directive::context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  std::string root_path = get_cwd();
  int64_t project_id = ectx.store->ensure_project(root_path);

  brain::Learner learner(ectx.store, nullptr, ectx.log);
  auto *matcher = learner.pattern_matcher();

  if (!matcher) {
    std::cerr << "Error: Pattern matcher not available\n";
    return 1;
  }

  auto patterns = matcher->list_patterns(project_id, 100);

  if (patterns.empty()) {
    std::cout << "No patterns learned for this project yet.\n";
    std::cout << "\n💡 Patterns are automatically created after 3+ similar "
                 "failures occur.\n";
    return 0;
  }

  std::cout << "\n";
  std::cout << "Learned Failure Patterns for " << root_path << "\n\n";

  std::cout << "ID   Name                              Type    Occurrences  "
               "Last Seen\n";
  std::cout << "────────────────────────────────────────────────────────────"
               "───────────────\n";

  auto now = std::chrono::system_clock::now();
  auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch())
                     .count();

  for (const auto &pattern : patterns) {
    // Calculate age
    int64_t age_sec = now_sec - pattern.last_seen;
    std::string age_str;
    if (age_sec < 3600) {
      age_str = std::to_string(age_sec / 60) + "m ago";
    } else if (age_sec < 86400) {
      age_str = std::to_string(age_sec / 3600) + "h ago";
    } else if (age_sec < 86400 * 7) {
      age_str = std::to_string(age_sec / 86400) + "d ago";
    } else if (age_sec < 86400 * 30) {
      age_str = std::to_string(age_sec / (86400 * 7)) + "w ago";
    } else {
      age_str = std::to_string(age_sec / (86400 * 30)) + "mo ago";
    }

    // Truncate name for display
    std::string name = pattern.name;
    if (name.length() > 33) {
      name = name.substr(0, 30) + "...";
    }

    printf("%-4lld %-33s %-7s %-12d %s\n", (long long)pattern.id, name.c_str(),
           pattern.failure_type.c_str(), pattern.occurrence_count,
           age_str.c_str());
  }

  std::cout << "\n";
  std::cout << patterns.size() << " pattern(s) learned\n";
  std::cout << "\nUse 'nazg brain pattern <id>' for pattern details\n";

  return 0;
}

// nazg brain pattern <id> - show pattern details
static int cmd_brain_pattern(const directive::command_context &ctx,
                              const directive::context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  if (ctx.argc < 1) {
    std::cerr << "Error: Missing pattern ID\n";
    std::cerr << "Usage: nazg brain pattern <id>\n";
    return 1;
  }

  int64_t pattern_id = std::stoll(ctx.argv[0]);

  brain::Learner learner(ectx.store, nullptr, ectx.log);
  auto *matcher = learner.pattern_matcher();

  if (!matcher) {
    std::cerr << "Error: Pattern matcher not available\n";
    return 1;
  }

  auto pattern_opt = matcher->get_pattern(pattern_id);

  if (!pattern_opt) {
    std::cerr << "Error: Pattern #" << pattern_id << " not found\n";
    return 1;
  }

  const auto &pattern = *pattern_opt;

  // Display pattern details
  std::cout << "\n";
  std::cout << "Pattern #" << pattern_id << ": " << pattern.name << "\n\n";

  std::cout << "Type:         " << pattern.failure_type << "\n";
  std::cout << "Occurrences:  " << pattern.occurrence_count << " times\n";

  // First/Last seen
  auto first_tp = std::chrono::system_clock::from_time_t(pattern.first_seen);
  std::time_t first_tt = std::chrono::system_clock::to_time_t(first_tp);
  auto last_tp = std::chrono::system_clock::from_time_t(pattern.last_seen);
  std::time_t last_tt = std::chrono::system_clock::to_time_t(last_tp);

  std::cout << "First Seen:   " << std::ctime(&first_tt);
  std::cout << "Last Seen:    " << std::ctime(&last_tt);

  std::cout << "\n";

  // Related failures
  if (!pattern.failure_ids.empty()) {
    std::cout << "Related Failures:\n";
    int count = 0;
    for (auto fid : pattern.failure_ids) {
      if (count >= 5)
        break; // Show first 5
      auto failure = ectx.store->get_failure(fid);
      if (failure) {
        std::cout << "  • Failure #" << fid;

        int64_t timestamp = std::stoll(failure->at("timestamp"));
        auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
        int64_t age = now_sec - timestamp;
        if (age < 3600 * 24) {
          std::cout << " (" << (age / 3600) << "h ago)";
        } else {
          std::cout << " (" << (age / 86400) << "d ago)";
        }

        if (failure->at("resolved") == "1") {
          std::cout << " - resolved";
        }
        std::cout << "\n";
      }
      count++;
    }

    if (pattern.failure_ids.size() > 5) {
      std::cout << "  ... and " << (pattern.failure_ids.size() - 5)
                << " more\n";
    }
    std::cout << "\n";
  }

  std::cout << "💡 Tip: Recovery suggestions will be available in Phase 3\n";

  return 0;
}

// nazg brain suggest <failure-id> - show recovery suggestions
static int cmd_brain_suggest(const directive::command_context &ctx,
                              const directive::context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  if (ctx.argc < 1) {
    std::cerr << "Error: Missing failure ID\n";
    std::cerr << "Usage: nazg brain suggest <failure-id>\n";
    return 1;
  }

  int64_t failure_id = std::stoll(ctx.argv[0]);

  std::string root_path = get_cwd();
  int64_t project_id = ectx.store->ensure_project(root_path);

  brain::Learner learner(ectx.store, nullptr, ectx.log);
  auto *matcher = learner.pattern_matcher();

  // Get failure details
  auto failure_opt = ectx.store->get_failure(failure_id);
  if (!failure_opt) {
    std::cerr << "Error: Failure #" << failure_id << " not found\n";
    return 1;
  }

  // Create recovery suggester
  brain::RecoverySuggester suggester(ectx.store, matcher, ectx.log);

  // Generate suggestions
  auto suggestions = suggester.suggest_recovery(project_id, failure_id, 5);

  if (suggestions.empty()) {
    std::cout << "\nNo recovery suggestions available for failure #"
              << failure_id << "\n";
    std::cout << "\n💡 Suggestions are generated from:\n";
    std::cout << "   • Matching failure patterns\n";
    std::cout << "   • Similar resolved failures\n";
    std::cout << "   • Generic recovery strategies\n";
    return 0;
  }

  std::cout << "\n";
  std::cout << "Recovery Suggestions for Failure #" << failure_id << "\n\n";

  int suggestion_num = 1;
  for (const auto &action : suggestions) {
    std::cout << suggestion_num << ". ";

    // Confidence indicator
    if (action.confidence_score >= 0.8) {
      std::cout << "⭐ ";
    } else if (action.confidence_score >= 0.6) {
      std::cout << "✓ ";
    }

    std::cout << action.description << "\n";

    std::cout << "   Confidence:  "
              << static_cast<int>(action.confidence_score * 100) << "%";

    if (action.success_count + action.failure_count > 0) {
      int total = action.success_count + action.failure_count;
      std::cout << " (" << action.success_count << "/" << total
                << " successes)";
    }
    std::cout << "\n";

    std::cout << "   Risk:        " << action.risk_level << "\n";

    if (action.avg_execution_time_ms > 0) {
      std::cout << "   Time:        ~"
                << (action.avg_execution_time_ms / 1000.0) << " seconds\n";
    }

    std::cout << "\n";
    suggestion_num++;
  }

  std::cout << "Apply a suggestion:\n";
  std::cout << "  nazg brain_recover " << failure_id
            << " --action 1        # Apply suggestion #1\n";
  std::cout << "  nazg brain_recover " << failure_id
            << " --dry-run         # Preview changes\n";

  return 0;
}

// nazg brain recover <failure-id> - execute recovery
static int cmd_brain_recover(const directive::command_context &ctx,
                              const directive::context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  if (ctx.argc < 1) {
    std::cerr << "Error: Missing failure ID\n";
    std::cerr << "Usage: nazg brain_recover <failure-id> [--action N] "
                 "[--dry-run]\n";
    return 1;
  }

  int64_t failure_id = std::stoll(ctx.argv[0]);

  // Parse options
  int action_num = 1; // Default to first suggestion
  bool dry_run = false;

  for (int i = 1; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--action" && i + 1 < ctx.argc) {
      action_num = std::stoi(ctx.argv[i + 1]);
      i++;
    } else if (arg == "--dry-run") {
      dry_run = true;
    }
  }

  std::string root_path = get_cwd();
  int64_t project_id = ectx.store->ensure_project(root_path);

  brain::Learner learner(ectx.store, nullptr, ectx.log);
  auto *matcher = learner.pattern_matcher();

  // Get failure details
  auto failure_opt = ectx.store->get_failure(failure_id);
  if (!failure_opt) {
    std::cerr << "Error: Failure #" << failure_id << " not found\n";
    return 1;
  }

  // Create recovery suggester
  brain::RecoverySuggester suggester(ectx.store, matcher, ectx.log);

  // Generate suggestions
  auto suggestions = suggester.suggest_recovery(project_id, failure_id, 5);

  if (suggestions.empty()) {
    std::cerr << "Error: No recovery suggestions available\n";
    return 1;
  }

  if (action_num < 1 || action_num > static_cast<int>(suggestions.size())) {
    std::cerr << "Error: Invalid action number " << action_num << "\n";
    std::cerr << "Available: 1 to " << suggestions.size() << "\n";
    return 1;
  }

  const auto &selected_action = suggestions[action_num - 1];

  std::cout << "\n";
  std::cout << "Recovering from Failure #" << failure_id << "\n\n";

  std::cout << "Selected Action: " << selected_action.description << "\n";
  std::cout << "Confidence: "
            << static_cast<int>(selected_action.confidence_score * 100)
            << "% | Risk: " << selected_action.risk_level << "\n";

  if (dry_run) {
    std::cout << "\n[DRY RUN MODE - No actual changes will be made]\n";
  }

  std::cout << "\n";

  if (!dry_run && selected_action.requires_confirmation) {
    std::cout << "Proceed with recovery? [y/N] ";
    std::string response;
    std::getline(std::cin, response);

    if (response != "y" && response != "Y" && response != "yes") {
      std::cout << "Recovery cancelled.\n";
      return 0;
    }
  }

  std::cout << "\nExecuting recovery...\n";

  // Execute recovery
  auto result = suggester.execute_recovery(project_id, failure_id,
                                            selected_action, dry_run);

  std::cout << "\n";

  if (result.success) {
    std::cout << "✓ Recovery completed successfully!\n";
  } else {
    std::cout << "✗ Recovery failed\n";
  }

  std::cout << "Time: " << (result.execution_time_ms / 1000.0) << " seconds\n";

  if (!result.actions_taken.empty()) {
    std::cout << "\nActions taken:\n";
    for (const auto &action : result.actions_taken) {
      std::cout << "  • " << action << "\n";
    }
  }

  if (!result.errors.empty()) {
    std::cout << "\nErrors:\n";
    for (const auto &error : result.errors) {
      std::cout << "  ✗ " << error << "\n";
    }
  }

  if (!result.output_log.empty()) {
    std::cout << "\nOutput:\n" << result.output_log << "\n";
  }

  std::cout << "\n";

  if (result.success) {
    std::cout << "💡 Mark failure as resolved:\n";
    std::cout << "   (Future: nazg brain resolve " << failure_id << ")\n";
  }

  return result.success ? 0 : 1;
}

void register_commands(directive::registry &reg, const directive::context &/*ctx*/) {
  directive::command_spec spec_build;
  spec_build.name = "build";
  spec_build.summary = "Smart build with change detection";
  spec_build.run = cmd_build;
  reg.add(spec_build);

  directive::command_spec spec_why;
  spec_why.name = "why";
  spec_why.summary = "Explain what nazg would do (dry-run analysis)";
  spec_why.run = cmd_why;
  reg.add(spec_why);

  directive::command_spec spec_status;
  spec_status.name = "build_status";
  spec_status.summary = "Show project status and detected facts";
  spec_status.run = cmd_build_status;
  reg.add(spec_status);

  directive::command_spec spec_facts;
  spec_facts.name = "build_facts";
  spec_facts.summary = "List all detected project facts";
  spec_facts.run = cmd_build_facts;
  reg.add(spec_facts);

  directive::command_spec spec_failures;
  spec_failures.name = "brain_failures";
  spec_failures.summary = "List recent failures with learning data";
  spec_failures.run = cmd_brain_failures;
  reg.add(spec_failures);

  directive::command_spec spec_failure;
  spec_failure.name = "brain_failure";
  spec_failure.summary = "Show detailed failure information";
  spec_failure.run = cmd_brain_failure;
  reg.add(spec_failure);

  directive::command_spec spec_patterns;
  spec_patterns.name = "brain_patterns";
  spec_patterns.summary = "List learned failure patterns";
  spec_patterns.run = cmd_brain_patterns;
  reg.add(spec_patterns);

  directive::command_spec spec_pattern;
  spec_pattern.name = "brain_pattern";
  spec_pattern.summary = "Show detailed pattern information";
  spec_pattern.run = cmd_brain_pattern;
  reg.add(spec_pattern);

  directive::command_spec spec_suggest;
  spec_suggest.name = "brain_suggest";
  spec_suggest.summary = "Suggest recovery actions for a failure";
  spec_suggest.run = cmd_brain_suggest;
  reg.add(spec_suggest);

  directive::command_spec spec_recover;
  spec_recover.name = "brain_recover";
  spec_recover.summary = "Execute recovery for a failure";
  spec_recover.run = cmd_brain_recover;
  reg.add(spec_recover);
}

} // namespace nazg::brain

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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

// Forward declaration - workspace integration in Phase 2
namespace nazg::workspace {
class Manager;
}

namespace nazg::brain {

// Forward declaration
class PatternMatcher;

// Failure Learning System - learns from build/test failures
// and suggests intelligent recovery actions
class Learner {
public:
  Learner(nexus::Store *store, workspace::Manager *workspace_mgr,
          blackbox::logger *log);
  ~Learner();

  // Failure context for recording
  struct FailureContext {
    std::string failure_type;    // "build", "test", "link", "runtime"
    std::string error_message;   // Raw error output
    std::string command;          // Command that failed
    int exit_code = 0;

    // State snapshots
    int64_t before_snapshot_id = 0; // Last known good state
    int64_t after_snapshot_id = 0;  // Failed state

    // Change detection
    std::vector<std::string> changed_files;
    std::map<std::string, std::string> changed_deps;
    std::map<std::string, std::string> changed_env;
    std::map<std::string, std::string> changed_system;

    // Optional context
    std::string error_location;  // file:line if known
    std::string severity = "medium"; // "low", "medium", "high", "critical"
    std::vector<std::string> tags;
  };

  // Record a failure with full context
  // Returns failure ID or 0 on error
  int64_t record_failure(int64_t project_id, const FailureContext &context);

  // Mark a failure as resolved
  bool mark_resolved(int64_t failure_id, const std::string &resolution_type,
                     int64_t resolution_snapshot_id, const std::string &notes,
                     bool success);

  // Automatic failure detection from command output
  std::optional<FailureContext> detect_failure(const std::string &command,
                                                const std::string &output,
                                                int exit_code);

  // Get failure statistics
  struct FailureStats {
    int total_failures = 0;
    int unresolved_failures = 0;
    int auto_resolved = 0;
    int manually_resolved = 0;
    std::map<std::string, int> by_type;    // "build" -> 42
    std::map<std::string, int> by_severity; // "high" -> 12
  };

  FailureStats get_statistics(int64_t project_id);

  // Get a specific failure by ID
  std::optional<std::map<std::string, std::string>>
  get_failure(int64_t failure_id);

  // List recent failures
  std::vector<std::map<std::string, std::string>>
  list_failures(int64_t project_id, int limit = 50);

  // List only unresolved failures
  std::vector<std::map<std::string, std::string>>
  list_unresolved_failures(int64_t project_id, int limit = 50);

  // Find similar past failures
  std::vector<std::map<std::string, std::string>>
  find_similar_failures(int64_t project_id, const std::string &error_signature,
                        int limit = 10);

  // Pattern access (delegates to PatternMatcher)
  PatternMatcher *pattern_matcher();

private:
  nexus::Store *store_;
  workspace::Manager *workspace_mgr_;
  blackbox::logger *log_;
  std::unique_ptr<PatternMatcher> pattern_matcher_;

  // Helper methods

  // Compute a normalized signature for error messages
  std::string compute_error_signature(const std::string &error_message);

  // Normalize error messages (remove paths, line numbers, timestamps)
  std::string normalize_error_message(const std::string &raw_error);

  // Extract file:line location from error message
  std::string extract_error_location(const std::string &error_message);

  // Infer failure type from command and error
  std::string infer_failure_type(const std::string &command,
                                   const std::string &error);

  // Extract relevant parts of error message for signature
  std::string extract_error_core(const std::string &error_message);

  // Convert changed data to JSON strings
  std::string changed_files_to_json(const std::vector<std::string> &files);
  std::string changed_deps_to_json(
      const std::map<std::string, std::string> &deps);
  std::string changed_env_to_json(
      const std::map<std::string, std::string> &env);
  std::string tags_to_json(const std::vector<std::string> &tags);
};

} // namespace nazg::brain

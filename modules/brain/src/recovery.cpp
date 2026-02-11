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

#include "brain/recovery.hpp"
#include "brain/pattern_matcher.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"
#include "task/executor.hpp"
#include "workspace/manager.hpp"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace nazg::brain {

RecoverySuggester::RecoverySuggester(nexus::Store *store,
                                      PatternMatcher *matcher,
                                      blackbox::logger *log)
    : store_(store), matcher_(matcher), log_(log) {}

std::vector<RecoverySuggester::RecoveryAction>
RecoverySuggester::suggest_recovery(int64_t project_id, int64_t failure_id,
                                     int max_suggestions) {
  std::vector<RecoveryAction> suggestions;

  if (!store_) {
    return suggestions;
  }

  // Get the failure details
  auto failure_opt = store_->get_failure(failure_id);
  if (!failure_opt) {
    return suggestions;
  }

  const auto &failure = *failure_opt;
  std::string error_signature = failure.at("error_signature");
  std::string error_message = failure.at("error_message");
  std::string failure_type = failure.at("failure_type");

  // Strategy 1: Get suggestions from matching patterns
  if (matcher_) {
    auto patterns = matcher_->find_matching_patterns(
        project_id, error_signature, failure_type, 0.7, error_message);

    for (const auto &pattern : patterns) {
      auto pattern_suggestions =
          generate_from_pattern(project_id, pattern.id, failure);
      suggestions.insert(suggestions.end(), pattern_suggestions.begin(),
                         pattern_suggestions.end());
    }
  }

  // Strategy 2: Get suggestions from similar resolved failures
  auto similar_suggestions =
      generate_from_similar_failures(project_id, error_signature, error_message);
  suggestions.insert(suggestions.end(), similar_suggestions.begin(),
                     similar_suggestions.end());

  // Strategy 3: Generic suggestions based on failure type
  auto generic_suggestions = generate_generic_suggestions(failure);
  suggestions.insert(suggestions.end(), generic_suggestions.begin(),
                     generic_suggestions.end());

  // Remove duplicates based on description
  std::sort(suggestions.begin(), suggestions.end(),
            [](const RecoveryAction &a, const RecoveryAction &b) {
              return a.confidence_score > b.confidence_score;
            });

  // Limit to max suggestions
  if (suggestions.size() > static_cast<size_t>(max_suggestions)) {
    suggestions.resize(max_suggestions);
  }

  return suggestions;
}

RecoverySuggester::RecoveryResult
RecoverySuggester::execute_recovery(int64_t project_id, int64_t failure_id,
                                     const RecoveryAction &action,
                                     bool dry_run) {
  RecoveryResult result;
  auto start_time = std::chrono::steady_clock::now();

  if (dry_run) {
    result.success = true;
    result.output_log = "[DRY RUN] Would execute: " + action.description;
    result.actions_taken.push_back("Dry run - no actual changes made");
    return result;
  }

  // Record recovery execution start
  int64_t history_id = 0;
  if (store_) {
    history_id = store_->begin_recovery_execution(
        project_id, failure_id, action.id,
        action.requires_confirmation ? "manual" : "auto");
  }

  // Execute based on action type
  bool exec_success = false;

  switch (action.type) {
  case ActionType::RESTORE_FILES:
    exec_success = execute_restore_files(action, project_id, result);
    break;

  case ActionType::RESTORE_SNAPSHOT:
    exec_success = execute_restore_snapshot(action, project_id, result);
    break;

  case ActionType::CLEAN_BUILD:
    exec_success = execute_clean_build(action, project_id, result);
    break;

  case ActionType::CUSTOM_COMMAND:
    exec_success = execute_custom_command(action, project_id, result);
    break;

  default:
    result.errors.push_back("Unsupported action type: " +
                            action_type_to_string(action.type));
    exec_success = false;
    break;
  }

  result.success = exec_success;

  // Calculate execution time
  auto end_time = std::chrono::steady_clock::now();
  result.execution_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                             start_time)
          .count();

  // Complete recovery execution record
  if (store_ && history_id > 0) {
    store_->complete_recovery_execution(history_id, result.success,
                                         result.output_log,
                                         result.verification_passed,
                                         result.execution_time_ms);
  }

  // Update action statistics
  if (action.id > 0 && store_) {
    update_action_stats(action.id, result.success, result.execution_time_ms);
  }

  return result;
}

RecoverySuggester::RecoveryResult
RecoverySuggester::auto_recover(int64_t project_id, int64_t failure_id,
                                 double confidence_threshold) {
  RecoveryResult result;

  // Get suggestions
  auto suggestions = suggest_recovery(project_id, failure_id, 5);

  if (suggestions.empty()) {
    result.errors.push_back("No recovery suggestions available");
    return result;
  }

  // Find highest confidence action that meets threshold
  for (const auto &action : suggestions) {
    if (action.confidence_score >= confidence_threshold &&
        !action.requires_confirmation) {

      if (log_) {
        log_->info("Brain",
                   "🤖 Auto-recovering with action: " + action.description +
                       " (confidence: " +
                       std::to_string(action.confidence_score * 100.0) + "%)");
      }

      result = execute_recovery(project_id, failure_id, action, false);

      if (result.success) {
        return result;
      }
    }
  }

  result.errors.push_back("No high-confidence auto-recovery actions available");
  return result;
}

bool RecoverySuggester::verify_recovery(int64_t /*project_id*/,
                                         const std::string &original_command) {
  if (!executor_ || original_command.empty()) {
    return true; // No executor or command to verify against
  }

  if (log_) {
    log_->info("Brain", "Verifying recovery by re-running: " + original_command);
  }

  auto result = executor_->execute_shell(original_command);
  return result.success;
}

std::vector<RecoverySuggester::RecoveryAction>
RecoverySuggester::get_actions_for_pattern(int64_t pattern_id) {
  std::vector<RecoveryAction> actions;

  if (!store_) {
    return actions;
  }

  auto rows = store_->get_recovery_actions_for_pattern(pattern_id);
  for (const auto &row : rows) {
    actions.push_back(parse_action(row));
  }

  return actions;
}

std::vector<RecoverySuggester::RecoveryAction>
RecoverySuggester::get_actions_for_failure(int64_t failure_id) {
  std::vector<RecoveryAction> actions;

  if (!store_) {
    return actions;
  }

  auto rows = store_->get_recovery_actions_for_failure(failure_id);
  for (const auto &row : rows) {
    actions.push_back(parse_action(row));
  }

  return actions;
}

bool RecoverySuggester::update_action_stats(int64_t action_id, bool success,
                                             int64_t execution_time_ms) {
  if (!store_) {
    return false;
  }
  return store_->update_recovery_action_stats(action_id, success,
                                               execution_time_ms);
}

// ===== Private: Suggestion Generation =====

std::vector<RecoverySuggester::RecoveryAction>
RecoverySuggester::generate_from_pattern(
    int64_t /*project_id*/, int64_t pattern_id,
    const std::map<std::string, std::string> &/*failure*/) {

  std::vector<RecoveryAction> suggestions;

  // Get existing actions for this pattern from database
  auto existing_actions = get_actions_for_pattern(pattern_id);
  if (!existing_actions.empty()) {
    return existing_actions;
  }

  // Generate new suggestions based on pattern type and history
  auto pattern_opt = matcher_ ? matcher_->get_pattern(pattern_id) : std::nullopt;
  if (!pattern_opt) {
    return suggestions;
  }

  const auto &pattern = *pattern_opt;

  // If pattern has related failures, check their resolutions
  if (!pattern.failure_ids.empty() && store_) {
    for (auto fid : pattern.failure_ids) {
      auto related_failure = store_->get_failure(fid);
      if (related_failure && related_failure->at("resolved") == "1") {
        // This failure was resolved - extract the resolution method
        std::string resolution_type = related_failure->at("resolution_type");
        int64_t resolution_snapshot =
            std::stoll(related_failure->at("resolution_snapshot_id"));

        if (resolution_type == "restore" && resolution_snapshot > 0) {
          auto action = create_restore_snapshot_action(
              resolution_snapshot,
              "Restore from snapshot that fixed this issue before");
          action.confidence_score =
              calculate_confidence(action, pattern.occurrence_count);
          suggestions.push_back(action);
          break; // Only suggest the first successful resolution
        }
      }
    }
  }

  return suggestions;
}

std::vector<RecoverySuggester::RecoveryAction>
RecoverySuggester::generate_from_similar_failures(
    int64_t project_id, const std::string &error_signature,
    const std::string &error_message) {

  std::vector<RecoveryAction> suggestions;

  if (!matcher_ || !store_) {
    return suggestions;
  }

  // Find similar failures that were resolved
  auto similar =
      matcher_->find_similar_failures(project_id, error_signature, error_message, 5);

  for (const auto &sim : similar) {
    if (sim.was_resolved && !sim.resolution_type.empty()) {
      auto failure = store_->get_failure(sim.failure_id);
      if (!failure) {
        continue;
      }

      if (sim.resolution_type == "restore") {
        int64_t snapshot_id =
            std::stoll(failure->at("resolution_snapshot_id"));
        if (snapshot_id > 0) {
          auto action = create_restore_snapshot_action(
              snapshot_id, "Similar failure was fixed by restoring this snapshot");
          action.confidence_score = sim.similarity_score * 0.8; // Discount slightly
          suggestions.push_back(action);
        }
      }
    }
  }

  return suggestions;
}

std::vector<RecoverySuggester::RecoveryAction>
RecoverySuggester::generate_generic_suggestions(
    const std::map<std::string, std::string> &failure) {

  std::vector<RecoveryAction> suggestions;

  std::string failure_type = failure.at("failure_type");
  std::string error_message = failure.at("error_message");

  // Generic build failure suggestions
  if (failure_type == "build") {
    // Suggest clean build
    auto clean_action =
        create_clean_build_action("Clean build often resolves build cache issues");
    clean_action.confidence_score = 0.4;
    clean_action.risk_level = "low";
    suggestions.push_back(clean_action);

    // Check for specific error patterns
    if (error_message.find("undefined reference") != std::string::npos ||
        error_message.find("unresolved external") != std::string::npos) {
      // Linker error - might need dependency restore
      int64_t before_snapshot = std::stoll(failure.at("before_snapshot_id"));
      if (before_snapshot > 0) {
        auto restore_action = create_restore_files_action(
            before_snapshot, {},
            "Restore dependencies from last successful build");
        restore_action.confidence_score = 0.5;
        suggestions.push_back(restore_action);
      }
    }
  }

  // Generic test failure suggestions
  if (failure_type == "test") {
    // Suggest restoring to before state
    int64_t before_snapshot = std::stoll(failure.at("before_snapshot_id"));
    if (before_snapshot > 0) {
      auto restore_action = create_restore_snapshot_action(
          before_snapshot, "Restore to last known passing state");
      restore_action.confidence_score = 0.6;
      suggestions.push_back(restore_action);
    }
  }

  return suggestions;
}

// ===== Private: Action Creation =====

RecoverySuggester::RecoveryAction
RecoverySuggester::create_restore_files_action(
    int64_t snapshot_id, const std::vector<std::string> &/*changed_files*/,
    const std::string &reason) {

  RecoveryAction action;
  action.type = ActionType::RESTORE_FILES;
  action.description = "Restore files from snapshot #" + std::to_string(snapshot_id);
  if (!reason.empty()) {
    action.description += " (" + reason + ")";
  }
  action.params["snapshot_id"] = std::to_string(snapshot_id);
  action.requires_confirmation = true;
  action.risk_level = "low";

  return action;
}

RecoverySuggester::RecoveryAction
RecoverySuggester::create_restore_snapshot_action(int64_t snapshot_id,
                                                   const std::string &reason) {
  RecoveryAction action;
  action.type = ActionType::RESTORE_SNAPSHOT;
  action.description =
      "Restore workspace from snapshot #" + std::to_string(snapshot_id);
  if (!reason.empty()) {
    action.description += " (" + reason + ")";
  }
  action.params["snapshot_id"] = std::to_string(snapshot_id);
  action.requires_confirmation = true;
  action.risk_level = "medium";

  return action;
}

RecoverySuggester::RecoveryAction
RecoverySuggester::create_clean_build_action(const std::string &reason) {
  RecoveryAction action;
  action.type = ActionType::CLEAN_BUILD;
  action.description = "Clean build directory and rebuild";
  if (!reason.empty()) {
    action.description += " (" + reason + ")";
  }
  action.requires_confirmation = false; // Safe to do automatically
  action.risk_level = "low";

  return action;
}

RecoverySuggester::RecoveryAction
RecoverySuggester::create_custom_command_action(const std::string &command,
                                                 const std::string &description) {
  RecoveryAction action;
  action.type = ActionType::CUSTOM_COMMAND;
  action.description = description.empty() ? "Run: " + command : description;
  action.params["command"] = command;
  action.requires_confirmation = true;
  action.risk_level = "high";

  return action;
}

// ===== Private: Execution Strategies =====

bool RecoverySuggester::execute_restore_files(const RecoveryAction &action,
                                               int64_t project_id,
                                               RecoveryResult &result) {
  if (!workspace_mgr_) {
    result.errors.push_back("No workspace manager configured");
    return false;
  }

  auto it = action.params.find("snapshot_id");
  if (it == action.params.end()) {
    result.errors.push_back("No snapshot_id in action params");
    return false;
  }

  int64_t snapshot_id = std::stoll(it->second);

  workspace::Manager::RestoreOptions opts;
  opts.restore_type = "partial";
  opts.dry_run = false;
  opts.interactive = false;

  if (log_) {
    log_->info("Brain", "Restoring files from snapshot #" + std::to_string(snapshot_id));
  }

  auto restore_result = workspace_mgr_->restore(project_id, snapshot_id, opts);
  result.actions_taken.push_back("Restored " + std::to_string(restore_result.files_restored) + " files from snapshot #" + std::to_string(snapshot_id));
  result.output_log = "Partial restore: " + std::to_string(restore_result.files_restored) + " files restored, " + std::to_string(restore_result.files_skipped) + " skipped";

  for (const auto &err : restore_result.errors) {
    result.errors.push_back(err);
  }

  return restore_result.success;
}

bool RecoverySuggester::execute_restore_snapshot(const RecoveryAction &action,
                                                  int64_t project_id,
                                                  RecoveryResult &result) {
  if (!workspace_mgr_) {
    result.errors.push_back("No workspace manager configured");
    return false;
  }

  auto it = action.params.find("snapshot_id");
  if (it == action.params.end()) {
    result.errors.push_back("No snapshot_id in action params");
    return false;
  }

  int64_t snapshot_id = std::stoll(it->second);

  workspace::Manager::RestoreOptions opts;
  opts.restore_type = "full";
  opts.dry_run = false;
  opts.interactive = false;

  if (log_) {
    log_->info("Brain", "Full restore from snapshot #" + std::to_string(snapshot_id));
  }

  auto restore_result = workspace_mgr_->restore(project_id, snapshot_id, opts);
  result.actions_taken.push_back("Full restore from snapshot #" + std::to_string(snapshot_id));
  result.output_log = "Full restore: " + std::to_string(restore_result.files_restored) + " files restored";

  for (const auto &err : restore_result.errors) {
    result.errors.push_back(err);
  }

  return restore_result.success;
}

bool RecoverySuggester::execute_clean_build(const RecoveryAction &/*action*/,
                                             int64_t /*project_id*/,
                                             RecoveryResult &result) {
  if (!executor_) {
    result.errors.push_back("No executor configured");
    return false;
  }

  if (log_) {
    log_->info("Brain", "Executing clean build: rm -rf build && cmake -S . -B build && cmake --build build");
  }

  auto exec_result = executor_->execute_shell(
      "rm -rf build && cmake -S . -B build && cmake --build build");

  result.actions_taken.push_back("Ran clean build (rm -rf build && cmake configure && build)");
  result.output_log = exec_result.stdout_output;

  if (!exec_result.success) {
    result.errors.push_back("Clean build failed with exit code " +
                            std::to_string(exec_result.exit_code));
  }

  return exec_result.success;
}

bool RecoverySuggester::execute_custom_command(const RecoveryAction &action,
                                                int64_t /*project_id*/,
                                                RecoveryResult &result) {
  if (!executor_) {
    result.errors.push_back("No executor configured");
    return false;
  }

  auto it = action.params.find("command");
  if (it == action.params.end() || it->second.empty()) {
    result.errors.push_back("No command specified in action params");
    return false;
  }

  const std::string &command = it->second;

  if (log_) {
    log_->info("Brain", "Executing custom recovery command: " + command);
  }

  auto exec_result = executor_->execute_shell(command);

  result.actions_taken.push_back("Ran command: " + command);
  result.output_log = exec_result.stdout_output;

  if (!exec_result.success) {
    result.errors.push_back("Command failed with exit code " +
                            std::to_string(exec_result.exit_code));
  }

  return exec_result.success;
}

// ===== Private: Confidence Scoring =====

double RecoverySuggester::calculate_confidence(const RecoveryAction &action,
                                                int pattern_occurrences) {
  double confidence = 0.0;

  // Base confidence from success rate
  int total_attempts = action.success_count + action.failure_count;
  if (total_attempts > 0) {
    confidence = static_cast<double>(action.success_count) / total_attempts;
  } else {
    // No history - use pattern occurrence count as proxy
    confidence = std::min(0.5 + (pattern_occurrences * 0.1), 0.9);
  }

  return confidence;
}

// ===== Private: Utility Functions =====

std::string
RecoverySuggester::action_type_to_string(ActionType type) {
  switch (type) {
  case ActionType::RESTORE_FILES:
    return "restore_files";
  case ActionType::RESTORE_SNAPSHOT:
    return "restore_snapshot";
  case ActionType::MODIFY_CONFIG:
    return "modify_config";
  case ActionType::DOWNGRADE_DEPENDENCY:
    return "downgrade_dep";
  case ActionType::RESTORE_ENVIRONMENT:
    return "restore_env";
  case ActionType::CLEAN_BUILD:
    return "clean_build";
  case ActionType::CUSTOM_COMMAND:
    return "custom_command";
  default:
    return "unknown";
  }
}

RecoverySuggester::ActionType
RecoverySuggester::string_to_action_type(const std::string &str) {
  if (str == "restore_files")
    return ActionType::RESTORE_FILES;
  if (str == "restore_snapshot")
    return ActionType::RESTORE_SNAPSHOT;
  if (str == "modify_config")
    return ActionType::MODIFY_CONFIG;
  if (str == "downgrade_dep")
    return ActionType::DOWNGRADE_DEPENDENCY;
  if (str == "restore_env")
    return ActionType::RESTORE_ENVIRONMENT;
  if (str == "clean_build")
    return ActionType::CLEAN_BUILD;
  if (str == "custom_command")
    return ActionType::CUSTOM_COMMAND;

  return ActionType::CUSTOM_COMMAND; // Default
}

RecoverySuggester::RecoveryAction
RecoverySuggester::parse_action(const std::map<std::string, std::string> &row) {
  RecoveryAction action;

  action.id = std::stoll(row.at("id"));
  action.type = string_to_action_type(row.at("action_type"));
  action.description = row.at("action_description");
  action.params = json_to_params(row.at("action_params_json"));

  action.success_count = std::stoi(row.at("success_count"));
  action.failure_count = std::stoi(row.at("failure_count"));
  action.avg_execution_time_ms = std::stoll(row.at("avg_execution_time_ms"));

  action.confidence_score = std::stod(row.at("confidence_score"));
  action.requires_confirmation = row.at("requires_user_confirmation") == "1";

  return action;
}

std::string RecoverySuggester::params_to_json(
    const std::map<std::string, std::string> &params) {
  if (params.empty()) {
    return "{}";
  }

  std::ostringstream json;
  json << "{";
  bool first = true;
  for (const auto &kv : params) {
    if (!first)
      json << ",";
    json << "\"" << kv.first << "\":\"" << kv.second << "\"";
    first = false;
  }
  json << "}";
  return json.str();
}

std::map<std::string, std::string>
RecoverySuggester::json_to_params(const std::string &json) {
  std::map<std::string, std::string> params;

  if (json.empty() || json == "{}") {
    return params;
  }

  // Simple JSON parser for {"key":"value", ...}
  // This is a simplified implementation
  std::string content = json;
  // Remove braces
  content.erase(std::remove(content.begin(), content.end(), '{'), content.end());
  content.erase(std::remove(content.begin(), content.end(), '}'), content.end());

  std::istringstream iss(content);
  std::string pair;
  while (std::getline(iss, pair, ',')) {
    size_t colon = pair.find(':');
    if (colon != std::string::npos) {
      std::string key = pair.substr(0, colon);
      std::string value = pair.substr(colon + 1);

      // Remove quotes
      key.erase(std::remove(key.begin(), key.end(), '"'), key.end());
      value.erase(std::remove(value.begin(), value.end(), '"'), value.end());

      // Trim whitespace
      key.erase(0, key.find_first_not_of(" \t\n\r"));
      key.erase(key.find_last_not_of(" \t\n\r") + 1);
      value.erase(0, value.find_first_not_of(" \t\n\r"));
      value.erase(value.find_last_not_of(" \t\n\r") + 1);

      params[key] = value;
    }
  }

  return params;
}

} // namespace nazg::brain

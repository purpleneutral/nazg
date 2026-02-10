#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::workspace {
class Manager;
}

namespace nazg::task {
class Executor;
}

namespace nazg::brain {

class PatternMatcher;

// Recovery Suggestion and Execution System
// Generates intelligent recovery actions based on learned patterns
class RecoverySuggester {
public:
  RecoverySuggester(nexus::Store *store, PatternMatcher *matcher,
                    blackbox::logger *log);

  // Recovery action types
  enum class ActionType {
    RESTORE_FILES,       // Restore specific files from snapshot
    RESTORE_SNAPSHOT,    // Full workspace restore
    MODIFY_CONFIG,       // Change configuration files
    DOWNGRADE_DEPENDENCY,// Revert dependency versions
    RESTORE_ENVIRONMENT, // Restore environment variables
    CLEAN_BUILD,         // Clean and rebuild
    CUSTOM_COMMAND       // Run custom recovery command
  };

  // Recovery action definition
  struct RecoveryAction {
    int64_t id = 0;              // Database ID (0 if not persisted)
    ActionType type;
    std::string description;     // Human-readable description
    std::map<std::string, std::string> params;

    // Confidence metrics
    double confidence_score = 0.0; // 0.0 to 1.0
    int success_count = 0;
    int failure_count = 0;
    int64_t avg_execution_time_ms = 0;

    // Safety
    bool requires_confirmation = true;
    std::string risk_level = "medium"; // "low", "medium", "high"
  };

  // Recovery execution result
  struct RecoveryResult {
    bool success = false;
    bool verification_passed = false;
    std::string output_log;
    int64_t execution_time_ms = 0;
    std::vector<std::string> actions_taken;
    std::vector<std::string> errors;
  };

  // Generate recovery suggestions for a failure
  std::vector<RecoveryAction> suggest_recovery(int64_t project_id,
                                                int64_t failure_id,
                                                int max_suggestions = 5);

  // Execute a specific recovery action
  RecoveryResult execute_recovery(int64_t project_id, int64_t failure_id,
                                   const RecoveryAction &action,
                                   bool dry_run = false);

  // Auto-recovery: applies best fix automatically if confidence is high enough
  RecoveryResult auto_recover(int64_t project_id, int64_t failure_id,
                               double confidence_threshold = 0.8);

  // Verify recovery success (re-run the original command)
  bool verify_recovery(int64_t project_id, const std::string &original_command);

  // Inject dependencies for execution strategies
  void set_workspace_manager(workspace::Manager *mgr) { workspace_mgr_ = mgr; }
  void set_executor(task::Executor *exec) { executor_ = exec; }

  // Get recovery actions from database
  std::vector<RecoveryAction> get_actions_for_pattern(int64_t pattern_id);
  std::vector<RecoveryAction> get_actions_for_failure(int64_t failure_id);

  // Update action statistics after execution
  bool update_action_stats(int64_t action_id, bool success,
                           int64_t execution_time_ms);

private:
  nexus::Store *store_;
  PatternMatcher *matcher_;
  blackbox::logger *log_;
  workspace::Manager *workspace_mgr_ = nullptr;
  task::Executor *executor_ = nullptr;

  // Suggestion generation strategies

  // Generate suggestions from matching patterns
  std::vector<RecoveryAction>
  generate_from_pattern(int64_t project_id, int64_t pattern_id,
                        const std::map<std::string, std::string> &failure);

  // Generate suggestions from similar resolved failures
  std::vector<RecoveryAction>
  generate_from_similar_failures(int64_t project_id,
                                  const std::string &error_signature,
                                  const std::string &error_message);

  // Generate generic suggestions based on failure type
  std::vector<RecoveryAction>
  generate_generic_suggestions(const std::map<std::string, std::string> &failure);

  // Action creation helpers

  RecoveryAction create_restore_files_action(
      int64_t snapshot_id, const std::vector<std::string> &changed_files,
      const std::string &reason);

  RecoveryAction create_restore_snapshot_action(int64_t snapshot_id,
                                                 const std::string &reason);

  RecoveryAction create_clean_build_action(const std::string &reason);

  RecoveryAction create_custom_command_action(const std::string &command,
                                               const std::string &description);

  // Execution strategies

  bool execute_restore_files(const RecoveryAction &action, int64_t project_id,
                              RecoveryResult &result);

  bool execute_restore_snapshot(const RecoveryAction &action,
                                 int64_t project_id, RecoveryResult &result);

  bool execute_clean_build(const RecoveryAction &action, int64_t project_id,
                           RecoveryResult &result);

  bool execute_custom_command(const RecoveryAction &action, int64_t project_id,
                               RecoveryResult &result);

  // Confidence scoring

  double calculate_confidence(const RecoveryAction &action,
                               int pattern_occurrences);

  // Utility functions

  std::string action_type_to_string(ActionType type);
  ActionType string_to_action_type(const std::string &str);

  // Parse recovery action from database row
  RecoveryAction parse_action(const std::map<std::string, std::string> &row);

  // Convert params map to JSON
  std::string params_to_json(const std::map<std::string, std::string> &params);
  std::map<std::string, std::string> json_to_params(const std::string &json);
};

} // namespace nazg::brain

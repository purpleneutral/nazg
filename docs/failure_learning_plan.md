# Failure Pattern Learning & Auto-Recovery Plan

## Overview

Elevate Nazg from a reactive assistant to an intelligent learning system that remembers failures, identifies patterns, and proactively suggests or applies fixes. By combining the brain module's detection capabilities with workspace snapshots and persistent failure history, Nazg becomes a safety net that learns from mistakes and prevents repeated failures.

## Motivation

### The Problem

Developers face recurring build/test failures:
- **Repeated mistakes**: "I upgraded this library before and it broke the same way"
- **Lost knowledge**: "Someone fixed this 6 months ago, but I don't remember how"
- **Time waste**: Hours debugging errors that have known solutions
- **Context loss**: "What changed between the working build and the broken build?"
- **Team friction**: Each developer re-learns the same lessons independently

### Current Limitations

Existing Nazg capabilities:
- ✅ Workspace snapshots capture state
- ✅ Brain detects project changes
- ✅ Nexus stores events and facts
- ❌ **No failure pattern recognition**
- ❌ **No learning from past mistakes**
- ❌ **No intelligent recovery suggestions**
- ❌ **No automatic fix application**

### Nazg's Unique Position

Nazg already has the foundation:
- **Workspace snapshots** - Complete state capture before/after failures
- **Brain module** - Change detection and project understanding
- **Nexus persistence** - Event history and fact storage
- **Task execution** - Can test recovery strategies automatically

Adding failure learning transforms these into an intelligent recovery system.

## Goals

1. **Learn from failures** - Capture comprehensive failure context automatically
2. **Identify patterns** - Recognize similar failures across time and projects
3. **Suggest fixes** - Recommend proven solutions based on history
4. **Auto-recover** - With permission, automatically apply known fixes
5. **Share knowledge** - Team-wide failure database improves everyone's workflow

## Architecture

### High-Level Design

```
┌─────────────┐
│   Engine    │  (intercepts build/test failures)
└──────┬──────┘
       │
       v
┌─────────────┐     ┌──────────────┐
│   Brain     │────>│  Workspace   │  (captures failure state)
│  Learner    │     └──────────────┘
└──────┬──────┘            │
       │                   │
       v                   v
┌─────────────┐     ┌──────────────┐
│   Nexus     │<────│   Pattern    │  (matches similar failures)
│   Store     │     │   Matcher    │
└──────┬──────┘     └──────────────┘
       │
       v
┌─────────────┐     ┌──────────────┐
│  Recovery   │────>│     Task     │  (executes recovery actions)
│  Suggester  │     └──────────────┘
└─────────────┘
```

### Module Structure

```
modules/brain/
├── include/brain/
│   ├── learner.hpp           # NEW: Failure learning system
│   ├── pattern_matcher.hpp   # NEW: Pattern recognition
│   ├── recovery.hpp           # NEW: Recovery suggestion engine
│   └── [existing files]
└── src/
    ├── learner.cpp
    ├── pattern_matcher.cpp
    ├── recovery.cpp
    └── [existing files]
```

### Data Flow

```
1. Build/Test Fails
   └─> Engine captures failure context
       └─> Brain Learner records failure
           ├─> Workspace creates failure snapshot
           ├─> Brain analyzes what changed
           └─> Nexus stores failure record

2. Pattern Matching
   └─> Brain Pattern Matcher searches history
       ├─> Compute error signature
       ├─> Find similar past failures
       └─> Extract resolution patterns

3. Recovery Suggestion
   └─> Brain Recovery Suggester generates fixes
       ├─> Rank suggestions by success rate
       ├─> Build recovery action plan
       └─> Present to user (or auto-apply)

4. Recovery Execution
   └─> Task module executes recovery
       ├─> Restore workspace state
       ├─> Modify configuration
       ├─> Rebuild and verify
       └─> Record outcome (success/failure)

5. Learning Update
   └─> Brain updates pattern database
       ├─> Increment success counters
       ├─> Update confidence scores
       └─> Improve future suggestions
```

## Database Schema Extensions

### New Tables

```sql
-- Failure records with complete context
CREATE TABLE brain_failures (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER REFERENCES projects(id),

  -- Failure identification
  failure_type TEXT NOT NULL,           -- "build", "test", "runtime", "link"
  error_signature TEXT NOT NULL,        -- Normalized hash of error pattern
  error_message TEXT,                   -- Full error output
  error_location TEXT,                  -- File:line if applicable

  -- State capture
  before_snapshot_id INTEGER REFERENCES workspace_snapshots(id),
  after_snapshot_id INTEGER REFERENCES workspace_snapshots(id),
  brain_snapshot_id INTEGER,

  -- Change analysis
  changed_files_json TEXT,              -- JSON array of modified files
  changed_deps_json TEXT,               -- JSON map of dependency changes
  changed_env_json TEXT,                -- JSON map of env var changes
  changed_system_json TEXT,             -- JSON map of system info changes

  -- Context
  command_executed TEXT,                -- Build/test command that failed
  exit_code INTEGER,
  timestamp INTEGER NOT NULL,

  -- Resolution tracking
  resolved BOOLEAN DEFAULT 0,
  resolved_at INTEGER,
  resolution_type TEXT,                 -- "restore", "manual", "auto", "config"
  resolution_snapshot_id INTEGER REFERENCES workspace_snapshots(id),
  resolution_notes TEXT,
  resolution_success BOOLEAN,

  -- Metadata
  severity TEXT DEFAULT 'medium',       -- "low", "medium", "high", "critical"
  tags TEXT,                            -- Comma-separated tags

  FOREIGN KEY(project_id) REFERENCES projects(id)
);

CREATE INDEX idx_brain_failures_signature ON brain_failures(error_signature);
CREATE INDEX idx_brain_failures_project ON brain_failures(project_id);
CREATE INDEX idx_brain_failures_type ON brain_failures(failure_type);
CREATE INDEX idx_brain_failures_timestamp ON brain_failures(timestamp);

-- Pattern definitions learned from failures
CREATE TABLE brain_failure_patterns (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER REFERENCES projects(id),

  -- Pattern identification
  pattern_signature TEXT UNIQUE NOT NULL, -- Hash of pattern characteristics
  pattern_name TEXT,                      -- Human-readable name
  failure_type TEXT NOT NULL,

  -- Pattern characteristics
  error_regex TEXT,                       -- Regex matching error messages
  trigger_conditions_json TEXT,           -- JSON: what changes trigger this

  -- Statistical data
  occurrence_count INTEGER DEFAULT 1,
  first_seen INTEGER NOT NULL,
  last_seen INTEGER NOT NULL,

  -- Related failures
  failure_ids_json TEXT,                  -- JSON array of failure IDs

  -- Resolution strategies
  resolution_strategies_json TEXT,        -- JSON array of known fixes

  FOREIGN KEY(project_id) REFERENCES projects(id)
);

CREATE INDEX idx_brain_patterns_signature ON brain_failure_patterns(pattern_signature);
CREATE INDEX idx_brain_patterns_project ON brain_failure_patterns(project_id);

-- Recovery actions and their success rates
CREATE TABLE brain_recovery_actions (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  pattern_id INTEGER REFERENCES brain_failure_patterns(id),
  failure_id INTEGER REFERENCES brain_failures(id),

  -- Action definition
  action_type TEXT NOT NULL,              -- "restore_files", "restore_snapshot",
                                          -- "modify_config", "downgrade_dep"
  action_params_json TEXT,                -- JSON: parameters for action
  action_description TEXT,                -- Human-readable description

  -- Execution
  attempted_count INTEGER DEFAULT 0,
  success_count INTEGER DEFAULT 0,
  failure_count INTEGER DEFAULT 0,

  -- Timing
  avg_execution_time_ms INTEGER,

  -- Metadata
  confidence_score REAL DEFAULT 0.0,      -- 0.0 to 1.0
  requires_user_confirmation BOOLEAN DEFAULT 1,
  created_at INTEGER NOT NULL,
  last_attempted INTEGER,

  FOREIGN KEY(pattern_id) REFERENCES brain_failure_patterns(id),
  FOREIGN KEY(failure_id) REFERENCES brain_failures(id)
);

CREATE INDEX idx_brain_recovery_pattern ON brain_recovery_actions(pattern_id);
CREATE INDEX idx_brain_recovery_confidence ON brain_recovery_actions(confidence_score);

-- Recovery execution history
CREATE TABLE brain_recovery_history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER REFERENCES projects(id),
  failure_id INTEGER REFERENCES brain_failures(id),
  action_id INTEGER REFERENCES brain_recovery_actions(id),

  -- Execution details
  started_at INTEGER NOT NULL,
  completed_at INTEGER,
  success BOOLEAN,
  execution_time_ms INTEGER,

  -- Results
  output_log TEXT,
  verification_passed BOOLEAN,

  -- What was done
  restored_files_json TEXT,               -- JSON array of files restored
  modified_configs_json TEXT,             -- JSON map of config changes

  -- Metadata
  execution_mode TEXT,                    -- "auto", "manual", "suggested"
  user_approved BOOLEAN DEFAULT 0,

  FOREIGN KEY(project_id) REFERENCES projects(id),
  FOREIGN KEY(failure_id) REFERENCES brain_failures(id),
  FOREIGN KEY(action_id) REFERENCES brain_recovery_actions(id)
);

CREATE INDEX idx_brain_recovery_history_failure ON brain_recovery_history(failure_id);
CREATE INDEX idx_brain_recovery_history_timestamp ON brain_recovery_history(started_at);

-- Knowledge graph: relationships between failures, patterns, and resolutions
CREATE TABLE brain_failure_relationships (
  id INTEGER PRIMARY KEY AUTOINCREMENT,

  source_failure_id INTEGER REFERENCES brain_failures(id),
  related_failure_id INTEGER REFERENCES brain_failures(id),

  relationship_type TEXT NOT NULL,        -- "similar", "caused_by", "preceded_by"
  similarity_score REAL,                  -- 0.0 to 1.0

  created_at INTEGER NOT NULL,

  FOREIGN KEY(source_failure_id) REFERENCES brain_failures(id),
  FOREIGN KEY(related_failure_id) REFERENCES brain_failures(id)
);

CREATE INDEX idx_brain_relationships_source ON brain_failure_relationships(source_failure_id);
CREATE INDEX idx_brain_relationships_related ON brain_failure_relationships(related_failure_id);
```

### Nexus Store Extensions

```cpp
// modules/nexus/include/nexus/store.hpp additions

namespace nazg::nexus {

class Store {
public:
  // ===== Brain Failures =====

  int64_t record_failure(
    int64_t project_id,
    const std::string& failure_type,
    const std::string& error_signature,
    const std::string& error_message,
    const std::map<std::string, std::string>& context
  );

  std::optional<std::map<std::string, std::string>>
  get_failure(int64_t failure_id);

  std::vector<std::map<std::string, std::string>>
  list_failures(int64_t project_id, int limit = 50);

  std::vector<std::map<std::string, std::string>>
  find_similar_failures(
    int64_t project_id,
    const std::string& error_signature,
    double similarity_threshold = 0.7
  );

  bool update_failure_resolution(
    int64_t failure_id,
    const std::string& resolution_type,
    int64_t resolution_snapshot_id,
    const std::string& notes,
    bool success
  );

  // ===== Failure Patterns =====

  int64_t record_pattern(
    int64_t project_id,
    const std::string& pattern_signature,
    const std::string& pattern_name,
    const std::string& failure_type,
    const std::string& error_regex,
    const std::string& trigger_conditions_json
  );

  std::optional<std::map<std::string, std::string>>
  get_pattern_by_signature(const std::string& pattern_signature);

  std::vector<std::map<std::string, std::string>>
  list_patterns(int64_t project_id);

  bool update_pattern_statistics(
    int64_t pattern_id,
    int occurrence_delta = 1
  );

  bool link_failure_to_pattern(
    int64_t failure_id,
    int64_t pattern_id
  );

  // ===== Recovery Actions =====

  int64_t add_recovery_action(
    int64_t pattern_id,
    const std::string& action_type,
    const std::string& action_params_json,
    const std::string& description,
    bool requires_confirmation = true
  );

  std::vector<std::map<std::string, std::string>>
  get_recovery_actions_for_pattern(
    int64_t pattern_id,
    bool order_by_confidence = true
  );

  bool update_recovery_action_stats(
    int64_t action_id,
    bool success,
    int64_t execution_time_ms
  );

  // ===== Recovery History =====

  int64_t begin_recovery_execution(
    int64_t project_id,
    int64_t failure_id,
    int64_t action_id,
    const std::string& execution_mode
  );

  bool complete_recovery_execution(
    int64_t history_id,
    bool success,
    const std::string& output_log,
    bool verification_passed
  );

  std::vector<std::map<std::string, std::string>>
  get_recovery_history(int64_t failure_id);

  // ===== Failure Relationships =====

  bool add_failure_relationship(
    int64_t source_failure_id,
    int64_t related_failure_id,
    const std::string& relationship_type,
    double similarity_score
  );

  std::vector<std::map<std::string, std::string>>
  get_related_failures(
    int64_t failure_id,
    const std::string& relationship_type = ""
  );
};

} // namespace nazg::nexus
```

## Core Classes

### brain::Learner

```cpp
// modules/brain/include/brain/learner.hpp

namespace nazg::brain {

class Learner {
public:
  Learner(nexus::Store* store,
          workspace::Manager* workspace_mgr,
          blackbox::logger* log);

  // Failure capture
  struct FailureContext {
    std::string failure_type;           // "build", "test", "link", "runtime"
    std::string error_message;          // Raw error output
    std::string command;                // Command that failed
    int exit_code;

    // State snapshots
    int64_t before_snapshot_id = 0;     // Last known good state
    int64_t after_snapshot_id = 0;      // Failed state

    // Change detection
    std::vector<std::string> changed_files;
    std::map<std::string, std::string> changed_deps;
    std::map<std::string, std::string> changed_env;

    // Optional context
    std::string error_location;         // file:line if known
    std::string severity = "medium";
    std::vector<std::string> tags;
  };

  // Record a failure with full context
  int64_t record_failure(int64_t project_id, const FailureContext& context);

  // Mark a failure as resolved
  bool mark_resolved(int64_t failure_id,
                     const std::string& resolution_type,
                     int64_t resolution_snapshot_id,
                     const std::string& notes,
                     bool success);

  // Automatic failure detection from command output
  std::optional<FailureContext> detect_failure(
    const std::string& command,
    const std::string& output,
    int exit_code
  );

  // Get failure statistics
  struct FailureStats {
    int total_failures;
    int unresolved_failures;
    int auto_resolved;
    int manually_resolved;
    std::map<std::string, int> by_type;
    std::map<std::string, int> by_severity;
  };

  FailureStats get_statistics(int64_t project_id);

private:
  nexus::Store* store_;
  workspace::Manager* workspace_mgr_;
  blackbox::logger* log_;

  // Helpers
  std::string compute_error_signature(const std::string& error_message);
  std::string normalize_error_message(const std::string& raw_error);
  std::string extract_error_location(const std::string& error_message);
  std::string infer_failure_type(const std::string& command,
                                   const std::string& error);
};

} // namespace nazg::brain
```

### brain::PatternMatcher

```cpp
// modules/brain/include/brain/pattern_matcher.hpp

namespace nazg::brain {

class PatternMatcher {
public:
  PatternMatcher(nexus::Store* store, blackbox::logger* log);

  // Pattern recognition
  struct Pattern {
    int64_t id;
    std::string signature;
    std::string name;
    std::string failure_type;
    std::string error_regex;

    // Statistics
    int occurrence_count;
    int64_t first_seen;
    int64_t last_seen;

    // Related failures
    std::vector<int64_t> failure_ids;

    // Known resolutions
    std::vector<RecoveryAction> resolution_strategies;
  };

  // Find matching patterns for a failure
  std::vector<Pattern> find_matching_patterns(
    int64_t project_id,
    const std::string& error_signature,
    const std::string& failure_type,
    double similarity_threshold = 0.7
  );

  // Create or update pattern from failures
  int64_t learn_pattern(
    int64_t project_id,
    const std::vector<int64_t>& similar_failure_ids,
    const std::string& pattern_name = ""
  );

  // Find similar past failures
  struct SimilarFailure {
    int64_t failure_id;
    double similarity_score;
    std::string error_message;
    int64_t timestamp;
    bool was_resolved;
    std::string resolution_type;
  };

  std::vector<SimilarFailure> find_similar_failures(
    int64_t project_id,
    const std::string& error_signature,
    int limit = 10
  );

  // Compute similarity between two failures
  double compute_similarity(
    const std::map<std::string, std::string>& failure_a,
    const std::map<std::string, std::string>& failure_b
  );

private:
  nexus::Store* store_;
  blackbox::logger* log_;

  // Pattern matching helpers
  bool matches_error_pattern(const std::string& error,
                             const std::string& pattern_regex);
  double levenshtein_similarity(const std::string& a, const std::string& b);
  std::string extract_error_pattern(const std::string& error_message);
  std::vector<std::string> tokenize_error(const std::string& error);
};

} // namespace nazg::brain
```

### brain::RecoverySuggester

```cpp
// modules/brain/include/brain/recovery.hpp

namespace nazg::brain {

class RecoverySuggester {
public:
  RecoverySuggester(nexus::Store* store,
                    workspace::Manager* workspace_mgr,
                    PatternMatcher* matcher,
                    blackbox::logger* log);

  // Recovery action types
  enum class ActionType {
    RESTORE_FILES,          // Restore specific files from snapshot
    RESTORE_SNAPSHOT,       // Full workspace restore
    MODIFY_CONFIG,          // Change configuration files
    DOWNGRADE_DEPENDENCY,   // Revert dependency versions
    RESTORE_ENVIRONMENT,    // Restore environment variables
    CLEAN_BUILD,            // Clean and rebuild
    CUSTOM_COMMAND          // Run custom recovery command
  };

  struct RecoveryAction {
    int64_t id;
    ActionType type;
    std::string description;
    std::map<std::string, std::string> params;

    // Confidence metrics
    double confidence_score;        // 0.0 to 1.0
    int success_count;
    int failure_count;
    int64_t avg_execution_time_ms;

    // Safety
    bool requires_confirmation;
    std::string risk_level;         // "low", "medium", "high"
  };

  // Generate recovery suggestions for a failure
  std::vector<RecoveryAction> suggest_recovery(
    int64_t project_id,
    int64_t failure_id,
    int max_suggestions = 5
  );

  // Execute a recovery action
  struct RecoveryResult {
    bool success;
    bool verification_passed;
    std::string output_log;
    int64_t execution_time_ms;
    std::vector<std::string> actions_taken;
    std::vector<std::string> errors;
  };

  RecoveryResult execute_recovery(
    int64_t project_id,
    int64_t failure_id,
    const RecoveryAction& action,
    bool dry_run = false
  );

  // Auto-recovery mode (applies best fix automatically)
  RecoveryResult auto_recover(
    int64_t project_id,
    int64_t failure_id,
    double confidence_threshold = 0.8
  );

  // Verify recovery success
  bool verify_recovery(
    int64_t project_id,
    const std::string& original_command
  );

private:
  nexus::Store* store_;
  workspace::Manager* workspace_mgr_;
  PatternMatcher* matcher_;
  blackbox::logger* log_;

  // Suggestion generation
  std::vector<RecoveryAction> generate_from_pattern(
    const PatternMatcher::Pattern& pattern
  );

  std::vector<RecoveryAction> generate_from_similar_failures(
    const std::vector<PatternMatcher::SimilarFailure>& similar
  );

  RecoveryAction create_restore_action(
    int64_t snapshot_id,
    const std::vector<std::string>& changed_files
  );

  // Execution helpers
  bool execute_restore_files(const RecoveryAction& action,
                             int64_t project_id,
                             RecoveryResult& result);

  bool execute_restore_snapshot(const RecoveryAction& action,
                                int64_t project_id,
                                RecoveryResult& result);

  bool execute_modify_config(const RecoveryAction& action,
                            int64_t project_id,
                            RecoveryResult& result);

  // Confidence scoring
  double calculate_confidence(const RecoveryAction& action,
                             const PatternMatcher::Pattern& pattern);
};

} // namespace nazg::brain
```

## Command Specifications

### nazg brain failures

List recent failures with context.

```bash
# Show recent failures
nazg brain failures

# Show all unresolved failures
nazg brain failures --unresolved

# Show failures by type
nazg brain failures --type build

# Show failures with details
nazg brain failures --verbose
```

**Output:**
```
Recent Failures for myproject

ID   Age      Type    Status      Error
────────────────────────────────────────────────────────────────
42   2m ago   build   unresolved  undefined reference to boost::filesystem
41   1h ago   test    resolved    assertion failed in tests/parser.cpp:45
40   3h ago   build   resolved    CMake could not find package Fmt
39   1d ago   link    unresolved  multiple definition of `main'

4 failures total (2 unresolved)
Use 'nazg brain failure <id>' for details
Use 'nazg brain suggest <id>' for recovery suggestions
```

### nazg brain failure

Show detailed information about a specific failure.

```bash
# Show failure details
nazg brain failure 42

# Show with related failures
nazg brain failure 42 --related
```

**Output:**
```
Failure #42 (Build Failure)

Occurred:    2 minutes ago
Status:      Unresolved
Command:     cmake --build build
Exit Code:   1
Severity:    medium

Error:
  undefined reference to `boost::filesystem::path::extension()'
  collect2: error: ld returned 1 exit status

Changes Since Last Success:
  Dependencies:
    ✓ boost upgraded: 1.81.0 → 1.82.0

  Files Modified:
    • src/engine/runtime.cpp
    • CMakeLists.txt

  Environment:
    No changes

Snapshots:
  Before:  #125 (@working) - 1 hour ago
  After:   #126 (auto-failure) - 2 minutes ago

Similar Failures:
  • Failure #31 (2 weeks ago) - Same error, resolved by restoring CMakeLists.txt
  • Failure #18 (1 month ago) - Similar boost upgrade issue

Suggested Actions:
  1. Restore CMakeLists.txt from snapshot #125 (90% confidence)
  2. Downgrade boost to 1.81.0 (75% confidence)
  3. Add Boost::filesystem to target_link_libraries (60% confidence)

Run 'nazg brain suggest 42' to see recovery options
Run 'nazg brain recover 42' to apply automatic fix
```

### nazg brain suggest

Generate recovery suggestions for a failure.

```bash
# Show suggestions
nazg brain suggest 42

# Show detailed action plans
nazg brain suggest 42 --verbose

# Filter by confidence
nazg brain suggest 42 --min-confidence 0.8
```

**Output:**
```
Recovery Suggestions for Failure #42

Based on 2 similar past failures and 1 matching pattern:

1. ⭐ Restore CMakeLists.txt from snapshot #125
   Confidence:  90% (3/3 successes)
   Risk:        Low
   Time:        ~5 seconds

   This fixed the same error 2 times before:
   • Failure #31 (2 weeks ago) - restored CMakeLists.txt → success
   • Failure #18 (1 month ago) - same action → success

   Action:
   $ nazg workspace restore @working --only CMakeLists.txt

2. Downgrade boost to 1.81.0
   Confidence:  75% (2/3 successes)
   Risk:        Medium (changes dependencies)
   Time:        ~30 seconds

   Pattern: "Boost ABI compatibility break"
   Boost 1.82 introduced filesystem API changes

   Action:
   $ conan install boost/1.81.0
   $ cmake --build build --clean-first

3. Add Boost::filesystem to link libraries
   Confidence:  60% (1/2 successes)
   Risk:        Low
   Time:        ~10 seconds

   The filesystem component may need explicit linking

   Action:
   Edit CMakeLists.txt:
   target_link_libraries(nazg PRIVATE Boost::filesystem)

Apply a suggestion:
  nazg brain recover 42 --action 1          # Apply suggestion #1
  nazg brain recover 42 --auto              # Auto-apply best fix
  nazg brain recover 42 --action 1 --dry-run  # Preview changes
```

### nazg brain recover

Execute recovery actions.

```bash
# Auto-recover (applies best fix with high confidence)
nazg brain recover 42 --auto

# Apply specific suggestion
nazg brain recover 42 --action 1

# Dry run (preview only)
nazg brain recover 42 --action 1 --dry-run

# Force application (skip confirmation)
nazg brain recover 42 --action 1 --yes

# Manual recovery guidance
nazg brain recover 42 --interactive
```

**Interactive Flow:**
```
Recovering from Failure #42 (Build Error)

Selected Action: Restore CMakeLists.txt from snapshot #125
Confidence: 90% | Risk: Low | Success Rate: 3/3

This will:
  ✓ Restore CMakeLists.txt to known-good state (1 hour ago)
  ✓ Keep all other files unchanged
  ✓ Create backup snapshot before restore

Similar past recoveries:
  • Failure #31: Applied same fix → build succeeded
  • Failure #18: Applied same fix → build succeeded

Proceed with recovery? [Y/n/v(iew diff)] v

--- CMakeLists.txt (current)
+++ CMakeLists.txt (snapshot #125)
@@ -156,7 +156,6 @@
-target_link_libraries(nazg PRIVATE Boost::system Boost::filesystem)
+target_link_libraries(nazg PRIVATE Boost::system)

Apply recovery? [Y/n] y

Executing recovery...
  ✓ Created backup snapshot #127
  ✓ Restored CMakeLists.txt from snapshot #125
  ✓ Running verification build...
  ✓ Build succeeded!

Recovery complete! (5.2s)
Failure #42 marked as resolved (recovery type: restore)

Summary:
  Files restored: 1
  Build status:   Success ✓
  Next steps:     Consider reviewing the boost filesystem usage
```

### nazg brain patterns

Manage learned failure patterns.

```bash
# List all patterns
nazg brain patterns

# Show pattern details
nazg brain pattern <pattern-id>

# List patterns by type
nazg brain patterns --type build

# Show patterns with statistics
nazg brain patterns --stats
```

**Output:**
```
Learned Failure Patterns for myproject

ID   Name                        Type    Occurrences  Last Seen
─────────────────────────────────────────────────────────────────
5    Boost ABI Compatibility     build   3            2m ago
4    Missing Link Library        link    7            1d ago
3    CMake Package Not Found     build   5            1w ago
2    Test Assertion Failure      test    12           2w ago
1    Python Import Error         test    4            1mo ago

5 patterns learned
Use 'nazg brain pattern <id>' for details and recovery strategies
```

### nazg brain learn

Manually teach Nazg about failures and fixes.

```bash
# Record a manual failure
nazg brain learn failure --type build --error "custom error"

# Teach a recovery action
nazg brain learn recovery <failure-id> --action "description" --command "fix command"

# Create a pattern from similar failures
nazg brain learn pattern --from-failures 42,45,47 --name "My Pattern"
```

### nazg brain stats

Show learning statistics.

```bash
# Overall statistics
nazg brain stats

# Detailed breakdown
nazg brain stats --detailed

# Recovery success rates
nazg brain stats --recovery
```

**Output:**
```
Brain Learning Statistics for myproject

Failures:
  Total recorded:        47
  Resolved:              39 (83%)
  Unresolved:            8 (17%)
  Auto-recovered:        23 (49%)
  Manually resolved:     16 (34%)

By Type:
  Build failures:        28 (60%)
  Test failures:         14 (30%)
  Link failures:         5 (10%)

Patterns:
  Learned patterns:      5
  Avg occurrences:       6.2 per pattern
  Most common:           "Test Assertion Failure" (12 times)

Recovery Success:
  Total attempts:        31
  Successful:            27 (87%)
  Failed:                4 (13%)
  Avg time:              8.4 seconds

  By Action Type:
    Restore files:       18/20 (90%)
    Downgrade deps:      5/7 (71%)
    Modify config:       4/4 (100%)

Learning Trends:
  ↓ Build failures down 40% this month
  ↑ Auto-recovery rate improved from 35% to 49%
  ⚡ Avg recovery time decreased from 15s to 8.4s
```

## Integration with Existing Modules

### Engine Integration

Auto-capture failures during builds and tests:

```cpp
// modules/engine/src/runtime.cpp

#include "brain/learner.hpp"

// In build/test execution
int result = system::execute_command(cmd);

if (result != 0) {
  // Capture failure automatically
  brain::Learner::FailureContext failure;
  failure.failure_type = (cmd.find("test") != std::string::npos) ? "test" : "build";
  failure.error_message = capture_output(cmd);
  failure.command = cmd;
  failure.exit_code = result;

  // Get before snapshot (last successful build)
  auto last_success = get_last_successful_snapshot(project_id);
  failure.before_snapshot_id = last_success;

  // Create after snapshot (current failed state)
  failure.after_snapshot_id = workspace_mgr->auto_snapshot(project_id, "failure");

  // Detect changes
  if (last_success > 0) {
    auto diff = workspace_mgr->diff_current(project_id, last_success);
    // Populate changed_files, changed_deps, changed_env from diff
  }

  // Record failure
  int64_t failure_id = learner->record_failure(project_id, failure);

  // Suggest recovery (if enabled)
  if (config->get_bool("brain.auto_suggest_recovery", true)) {
    auto suggestions = recovery_suggester->suggest_recovery(project_id, failure_id);
    display_suggestions(suggestions);

    // Auto-recover if high confidence
    if (config->get_bool("brain.auto_recover", false)) {
      for (const auto& suggestion : suggestions) {
        if (suggestion.confidence_score >= config->get_double("brain.auto_recover_threshold", 0.9)) {
          log->info("Brain", "Auto-recovering from failure with high-confidence fix");
          auto result = recovery_suggester->execute_recovery(project_id, failure_id, suggestion);
          if (result.success) {
            log->info("Brain", "Auto-recovery succeeded!");
            break;
          }
        }
      }
    }
  }
}
```

### Brain Module Extension

Extend existing brain components:

```cpp
// modules/brain/include/brain/planner.hpp

class Planner {
public:
  // Existing methods...

  // NEW: Enhanced planning with failure awareness
  Plan decide_with_learning(int64_t project_id,
                           const ProjectInfo& info,
                           const SnapshotResult& snapshot);

  // Check if current state matches a known failure pattern
  std::optional<int64_t> detect_risky_state(int64_t project_id);
};
```

```cpp
// modules/brain/src/planner.cpp

Plan Planner::decide_with_learning(int64_t project_id,
                                   const ProjectInfo& info,
                                   const SnapshotResult& snapshot) {
  // Standard planning
  Plan plan = decide(project_id, info, snapshot);

  // Check for risk indicators
  if (learner_) {
    auto risky = detect_risky_state(project_id);
    if (risky) {
      plan.reason += " ⚠ Warning: Current state similar to past failure #" +
                     std::to_string(*risky);
      plan.action = Action::WARN;
    }
  }

  return plan;
}
```

### Workspace Integration

Link failures to snapshots:

```cpp
// modules/workspace/src/manager.cpp

int64_t Manager::auto_snapshot(int64_t project_id, const std::string& trigger) {
  SnapshotOptions opts;
  opts.label = "auto-" + trigger;
  opts.trigger_type = trigger;

  // If this is a failure snapshot, mark it specially
  if (trigger == "failure" || trigger.find("pre-recovery") != std::string::npos) {
    opts.label += " [failure state]";
  }

  return create_snapshot(project_id, opts);
}
```

### Task Module Integration

Execute recovery actions:

```cpp
// modules/task/include/task/builder.hpp

class Builder {
public:
  // Existing methods...

  // NEW: Recovery execution
  struct RecoveryTask {
    int64_t failure_id;
    int64_t action_id;
    std::string action_type;
    std::map<std::string, std::string> params;
  };

  bool execute_recovery(const RecoveryTask& task);
};
```

## Configuration

Add to `~/.config/nazg/config.toml`:

```toml
[brain]
# Enable automatic failure capture
auto_capture_failures = true

# Auto-create snapshots before/after failures
failure_snapshots = true

# Enable recovery suggestions
auto_suggest_recovery = true

# Display suggestions after failures
show_suggestions = true

# Auto-recovery settings
auto_recover = false              # Don't auto-recover by default (safety)
auto_recover_threshold = 0.90     # Only auto-recover if 90%+ confidence

# Pattern learning
auto_learn_patterns = true
pattern_min_occurrences = 3       # Create pattern after 3 similar failures
pattern_similarity_threshold = 0.7

# Recovery settings
recovery_create_backup = true     # Always backup before recovery
recovery_verify_after = true      # Run verification after recovery
recovery_max_suggestions = 5      # Show top 5 suggestions

# History retention
keep_failure_history_days = 180   # Keep 6 months of history
keep_resolved_failures = true
prune_low_value_patterns = false  # Keep all patterns even if rare

[brain.failure_severity]
# Auto-classify failure severity
build_error = "high"
test_failure = "medium"
warning = "low"

[brain.notifications]
# Notify on specific events
notify_on_auto_recovery = true
notify_on_pattern_learned = true
notify_on_repeated_failure = true    # Same failure 3+ times
```

## Implementation Phases

### Phase 1: Foundation (Week 1)
- [ ] Database schema for brain_failures table
- [ ] Extend nexus::Store with failure recording methods
- [ ] Implement brain::Learner class
- [ ] Basic failure capture from command output
- [ ] Command: `nazg brain failures`
- [ ] Command: `nazg brain failure <id>`

### Phase 2: Pattern Recognition (Week 2)
- [ ] Database schema for brain_failure_patterns
- [ ] Implement brain::PatternMatcher class
- [ ] Error signature computation (normalization + hashing)
- [ ] Similarity scoring algorithm
- [ ] Find similar failures query
- [ ] Command: `nazg brain patterns`
- [ ] Command: `nazg brain pattern <id>`

### Phase 3: Recovery Suggestions (Week 3)
- [ ] Database schema for brain_recovery_actions
- [ ] Implement brain::RecoverySuggester class
- [ ] Generate suggestions from patterns
- [ ] Generate suggestions from similar failures
- [ ] Confidence scoring algorithm
- [ ] Command: `nazg brain suggest <failure-id>`

### Phase 4: Recovery Execution (Week 4)
- [ ] Database schema for brain_recovery_history
- [ ] Implement recovery action execution
- [ ] Restore files action
- [ ] Restore snapshot action
- [ ] Modify config action
- [ ] Downgrade dependency action
- [ ] Command: `nazg brain recover <failure-id>`
- [ ] Recovery verification

### Phase 5: Auto-Capture Integration (Week 5)
- [ ] Engine integration for auto-capture
- [ ] Hook into build command failures
- [ ] Hook into test command failures
- [ ] Auto-create before/after snapshots
- [ ] Workspace diff analysis for failures
- [ ] Auto-display suggestions after failures

### Phase 6: Auto-Recovery (Week 6)
- [ ] Safety checks for auto-recovery
- [ ] Confidence threshold enforcement
- [ ] Backup before auto-recovery
- [ ] Verification after auto-recovery
- [ ] Rollback on auto-recovery failure
- [ ] Configuration for auto-recovery mode

### Phase 7: Pattern Learning (Week 7)
- [ ] Automatic pattern creation
- [ ] Pattern statistics tracking
- [ ] Pattern refinement over time
- [ ] Failure relationship graph
- [ ] Similar failure clustering
- [ ] Command: `nazg brain learn pattern`

### Phase 8: Statistics & Polish (Week 8)
- [ ] Comprehensive statistics collection
- [ ] Success rate tracking
- [ ] Trend analysis
- [ ] Command: `nazg brain stats`
- [ ] Interactive recovery wizard
- [ ] Documentation and examples
- [ ] Performance optimization

## Usage Examples

### Example 1: Build Failure with Auto-Recovery

```bash
# Developer upgrades a library
$ conan install boost/1.82.0

# Attempt build
$ nazg build

⚙ Building myproject...
❌ Build failed (exit code 1)

Analyzing failure...
✓ Captured failure context
✓ Created failure snapshot #128
✓ Found 2 similar past failures
✓ Identified pattern: "Boost ABI Compatibility"

💡 Recovery suggestions:
   1. Restore CMakeLists.txt from snapshot #125 (90% confidence)
   2. Downgrade boost to 1.81.0 (75% confidence)

Apply suggestion #1? [Y/n/v(iew)] y

Recovering...
✓ Restored CMakeLists.txt
✓ Rebuild succeeded!

✨ Recovery complete in 5.2s
   Failure #42 resolved via automatic restore
```

### Example 2: Recurring Test Failure

```bash
# Run tests
$ nazg test

❌ Test failed: assertion in tests/parser.cpp:45

Analyzing failure...
⚠ This failure has occurred 4 times in the past month

Pattern detected: "Parser Null Pointer"
  Previous occurrences: #38, #35, #29, #22
  Common trigger: Changes to src/parser/tokenizer.cpp

💡 Known fix (used 3 times successfully):
   Add null check in tokenizer.cpp:156

Would you like to:
  1. View the fix that worked before
  2. Apply automatic fix (85% confidence)
  3. See failure history
  4. Manual debug

Choice: 1

Previous successful fix (Failure #38):
  File: src/parser/tokenizer.cpp:156
  Added: if (!current_token) return nullptr;

Apply similar fix? [y/N] y
✓ Applied fix
✓ Tests passed!
```

### Example 3: Learning from Manual Fix

```bash
# Build fails with custom error
$ nazg build
❌ Build failed: custom library not found

# Developer manually fixes it
$ export CUSTOM_LIB_PATH=/opt/custom/lib
$ nazg build
✓ Build succeeded

# Teach Nazg about the fix
$ nazg brain learn recovery --last-failure \
  --action "Set CUSTOM_LIB_PATH environment" \
  --command "export CUSTOM_LIB_PATH=/opt/custom/lib"

✓ Learned recovery action for failure #43
  Pattern "Missing Library Path" updated
  Next time this error occurs, Nazg will suggest this fix
```

### Example 4: Team Knowledge Sharing

```bash
# Developer A experiences failure and auto-recovers
$ nazg build
❌ Build failed: missing dependency
💡 Auto-recovering... (90% confidence)
✓ Recovery succeeded

# Developer B encounters same issue later
$ nazg build
❌ Build failed: missing dependency

Analyzing failure...
✓ Matched pattern learned from teammate's failure (#45)

💡 Your teammate resolved this 2 days ago by:
   "Adding missing dependency to CMakeLists.txt"
   Success rate: 1/1 (100%)

Apply same fix? [Y/n] y
✓ Fixed! (learned from team history)
```

### Example 5: Preventing Known Failures

```bash
# Developer modifies a file known to cause issues
$ vim src/engine/runtime.cpp

$ nazg brain check

⚠ Potential issue detected!

  Pattern: "Runtime Memory Leak"
  Risk: High

  Changes to src/engine/runtime.cpp have caused
  failures 3 times in the past:
    • Failure #50 (1 week ago)
    • Failure #41 (2 weeks ago)
    • Failure #33 (1 month ago)

  Common issue: Forgot to delete dynamically allocated object

  Suggestions:
    • Review destructor in runtime.cpp
    • Run valgrind before committing
    • Consider RAII/smart pointers

Continue anyway? [y/N]
```

## Success Metrics

### Quantitative Goals

1. **Failure Resolution Time**
   - Baseline: 30-120 minutes average debug time
   - Target: <5 minutes with auto-recovery suggestions
   - Stretch: <1 minute with auto-recovery enabled

2. **Auto-Recovery Success Rate**
   - Target: 80%+ success rate for high-confidence suggestions
   - Target: 90%+ for confidence > 0.9

3. **Developer Productivity**
   - Target: 50% reduction in time spent on repeated failures
   - Target: 2+ hours saved per developer per week

4. **Knowledge Retention**
   - Target: 100% of resolutions captured automatically
   - Target: Pattern library grows to cover 70%+ of failures within 3 months

### Qualitative Goals

1. **Developer Confidence**
   - "I can experiment without fear"
   - "Nazg has my back when things break"

2. **Team Collaboration**
   - "Team knowledge is automatically shared"
   - "New developers learn from past mistakes instantly"

3. **Reduced Frustration**
   - "No more 'I fixed this before but forgot how'"
   - "No more repeating the same debugging steps"

## Future Enhancements

### Post-MVP Features

1. **Cross-Project Learning**
   - Share patterns across different projects
   - Global pattern library
   - Community pattern marketplace

2. **Predictive Failure Prevention**
   - "These changes look risky based on history"
   - Suggest defensive snapshots proactively
   - Pre-emptive warnings before dangerous operations

3. **Machine Learning Integration**
   - Train ML model on failure corpus
   - More sophisticated pattern recognition
   - Better confidence scoring

4. **Visual Analytics**
   - Timeline visualization of failures
   - Failure clustering graphs
   - Success rate heat maps

5. **Integration with CI/CD**
   - Learn from CI failures
   - Suggest fixes before local execution
   - Team-wide failure dashboard

6. **Natural Language Queries**
   - "Show me all boost-related failures"
   - "What usually breaks when I upgrade cmake?"
   - "Why did my build fail?"

7. **Collaborative Recovery**
   - Real-time failure sharing in teams
   - "Alice just fixed this error 5 minutes ago"
   - Recovery action marketplace

8. **Advanced Recovery Actions**
   - Bisect to find breaking commit
   - Auto-generate minimal reproduction
   - Create unit test from failure

## Testing Strategy

### Unit Tests

```cpp
// tests/brain_learner_tests.cpp
TEST(BrainLearner, RecordFailure)
TEST(BrainLearner, ComputeErrorSignature)
TEST(BrainLearner, DetectFailureFromOutput)
TEST(BrainLearner, MarkResolved)

// tests/brain_pattern_matcher_tests.cpp
TEST(PatternMatcher, FindMatchingPatterns)
TEST(PatternMatcher, ComputeSimilarity)
TEST(PatternMatcher, LearnPattern)

// tests/brain_recovery_tests.cpp
TEST(RecoverySuggester, SuggestRecovery)
TEST(RecoverySuggester, ExecuteRecovery)
TEST(RecoverySuggester, VerifyRecovery)
```

### Integration Tests

```bash
# Test complete failure learning flow
./tests/integration/test_failure_learning.sh

# Test auto-recovery
./tests/integration/test_auto_recovery.sh

# Test pattern matching
./tests/integration/test_pattern_matching.sh
```

### Smoke Tests

Add to existing `tests/smoke.sh`:

```bash
# Test brain failure recording
test_brain_failure_recording() {
  # Cause intentional build failure
  # Verify failure is recorded
  # Check failure details are captured
}

# Test recovery suggestions
test_brain_suggestions() {
  # Record failure with known pattern
  # Verify suggestions are generated
  # Check confidence scores
}

# Test recovery execution
test_brain_recovery() {
  # Create failure scenario
  # Apply recovery suggestion
  # Verify recovery success
}
```

## Conclusion

The Failure Pattern Learning & Auto-Recovery system transforms Nazg from a helpful assistant into an intelligent safety net that:

1. **Never forgets** - Every failure and fix is permanently captured
2. **Learns from experience** - Patterns emerge automatically from history
3. **Shares knowledge** - Team benefits from each developer's fixes
4. **Prevents repetition** - "I fixed this before" becomes obsolete
5. **Recovers intelligently** - High-confidence fixes apply automatically

By leveraging Nazg's existing brain module (detection, planning), workspace module (snapshots, restore), and nexus persistence, this feature creates a complete learning loop:

```
Failure → Capture → Analyze → Learn → Suggest → Recover → Verify → Improve
```

This positions Nazg as not just a build assistant, but an intelligent development partner that gets smarter with every use.

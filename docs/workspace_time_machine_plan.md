# Workspace Time Machine Plan

## Overview

Transform Nazg into an essential safety net for developers by providing intelligent workspace state management. Unlike git which only tracks code, Nazg's workspace system captures complete development state including build artifacts, dependencies, environment, and configuration—enabling fearless experimentation with intelligent rollback.

## Motivation

Developers face constant risk during:
- Dependency upgrades that break builds
- Major refactors that introduce subtle bugs
- Build system changes (CMake → Meson)
- Environment configuration changes
- Git operations (rebase, merge conflicts)

Current solutions are inadequate:
- Git only tracks source code, not full workspace state
- Manual backups are tedious and rarely done
- VM snapshots are too heavyweight and slow
- No tool provides intelligent differential restore

Nazg's unique position:
- Already has snapshot system (brain module)
- Already tracks project state (nexus persistence)
- Already detects project changes (detector)
- Can layer intelligent restore on top of existing infrastructure

## Architecture

### High-Level Design

```
┌─────────────┐
│   Engine    │  (triggers auto-snapshots before risky ops)
└──────┬──────┘
       │
       v
┌─────────────┐     ┌──────────────┐
│  Workspace  │────>│    Brain     │  (snapshot system)
│   Manager   │     └──────────────┘
└──────┬──────┘            │
       │                   │
       v                   v
┌─────────────┐     ┌──────────────┐
│   Nexus     │<────│   System     │  (filesystem ops)
│   Store     │     └──────────────┘
└─────────────┘
```

### Module Structure

```
modules/workspace/
├── include/workspace/
│   ├── manager.hpp         # Main workspace manager
│   ├── snapshot.hpp        # Extended snapshot with artifacts
│   ├── differ.hpp          # State comparison & analysis
│   ├── restorer.hpp        # Intelligent restore logic
│   ├── commands.hpp        # CLI command handlers
│   └── types.hpp           # Workspace-specific types
└── src/
    ├── manager.cpp
    ├── snapshot.cpp
    ├── differ.cpp
    ├── restorer.cpp
    └── commands.cpp
```

## Database Schema Extensions

### New Tables

```sql
-- Workspace snapshots (extends brain snapshots)
CREATE TABLE workspace_snapshots (
  id INTEGER PRIMARY KEY,
  project_id INTEGER REFERENCES projects(id),
  brain_snapshot_id INTEGER,      -- Links to existing brain snapshots
  label TEXT,                      -- User-friendly name ("pre-refactor")
  trigger_type TEXT,               -- "auto", "manual", "pre-build", "pre-upgrade"
  timestamp INTEGER NOT NULL,

  -- Extended state capture
  build_dir_hash TEXT,             -- Hash of build artifacts
  deps_manifest_hash TEXT,         -- Hash of package.json, Cargo.lock, etc.
  env_snapshot TEXT,               -- JSON of relevant env vars
  system_info TEXT,                -- Compiler versions, system libs (JSON)

  -- Metadata
  restore_count INTEGER DEFAULT 0, -- How many times restored to this state
  is_clean_build BOOLEAN,          -- Was this after clean build?
  git_commit TEXT,                 -- Git commit at snapshot time
  git_branch TEXT,                 -- Git branch at snapshot time

  created_at INTEGER,
  tags TEXT                        -- Comma-separated user tags
);

-- Snapshot file manifest (what's in each snapshot)
CREATE TABLE workspace_files (
  id INTEGER PRIMARY KEY,
  snapshot_id INTEGER REFERENCES workspace_snapshots(id),
  file_path TEXT NOT NULL,         -- Relative to project root
  file_type TEXT,                  -- "source", "build", "dep", "config"
  file_hash TEXT,                  -- SHA256 of content
  file_size INTEGER,
  mtime INTEGER
);

-- Restore history (track what was restored when)
CREATE TABLE workspace_restores (
  id INTEGER PRIMARY KEY,
  project_id INTEGER REFERENCES projects(id),
  from_snapshot_id INTEGER REFERENCES workspace_snapshots(id),
  restore_type TEXT,               -- "full", "smart", "partial"
  files_restored INTEGER,
  timestamp INTEGER,
  reason TEXT,                     -- User-provided or auto ("build failed")
  success BOOLEAN,
  duration_ms INTEGER
);

-- Failure patterns (learn what changes cause failures)
CREATE TABLE workspace_failures (
  id INTEGER PRIMARY KEY,
  project_id INTEGER REFERENCES projects(id),
  failure_type TEXT,               -- "build", "test", "runtime"
  error_signature TEXT,            -- Hash of error message pattern
  error_message TEXT,

  -- State before failure
  before_snapshot_id INTEGER REFERENCES workspace_snapshots(id),
  after_snapshot_id INTEGER REFERENCES workspace_snapshots(id),

  -- What changed
  changed_files TEXT,              -- JSON array of file paths
  changed_deps TEXT,               -- JSON of dependency changes

  -- Resolution (if known)
  resolved BOOLEAN DEFAULT 0,
  resolution_type TEXT,            -- "restore", "fix", "upgrade"
  resolution_notes TEXT,

  timestamp INTEGER
);

-- Workspace tags (named checkpoints)
CREATE TABLE workspace_tags (
  id INTEGER PRIMARY KEY,
  project_id INTEGER REFERENCES projects(id),
  snapshot_id INTEGER REFERENCES workspace_snapshots(id),
  tag_name TEXT UNIQUE NOT NULL,   -- "working", "pre-refactor", "v1.0"
  description TEXT,
  created_at INTEGER
);
```

### Nexus Store Extensions

```cpp
// modules/nexus/include/nexus/store.hpp additions

// ===== Workspace Snapshots =====
int64_t add_workspace_snapshot(int64_t project_id,
                                int64_t brain_snapshot_id,
                                const WorkspaceSnapshot& ws);

std::optional<WorkspaceSnapshot> get_workspace_snapshot(int64_t id);
std::optional<WorkspaceSnapshot> latest_workspace_snapshot(int64_t project_id);
std::vector<WorkspaceSnapshot> list_workspace_snapshots(int64_t project_id,
                                                         int limit = 20);

// ===== Workspace Files =====
void add_workspace_file(int64_t snapshot_id, const WorkspaceFile& file);
std::vector<WorkspaceFile> get_workspace_files(int64_t snapshot_id);

// ===== Workspace Restores =====
int64_t begin_workspace_restore(int64_t project_id,
                                 int64_t snapshot_id,
                                 const std::string& restore_type);
void finish_workspace_restore(int64_t restore_id,
                               bool success,
                               int files_restored,
                               int64_t duration_ms);

// ===== Workspace Tags =====
bool tag_workspace_snapshot(int64_t snapshot_id,
                             const std::string& tag_name,
                             const std::string& description = "");
std::optional<int64_t> get_snapshot_by_tag(int64_t project_id,
                                            const std::string& tag_name);
void untag_workspace_snapshot(const std::string& tag_name);

// ===== Failure Patterns =====
void record_workspace_failure(int64_t project_id,
                               const WorkspaceFailure& failure);
std::vector<WorkspaceFailure> similar_failures(int64_t project_id,
                                                const std::string& error_sig);
```

## Core Classes

### WorkspaceManager

```cpp
// modules/workspace/include/workspace/manager.hpp

namespace nazg::workspace {

class Manager {
public:
  Manager(nexus::Store* store,
          brain::Detector* detector,
          blackbox::logger* log);

  // Create snapshots
  struct SnapshotOptions {
    std::string label;
    std::string trigger_type = "manual";
    bool include_build_dir = true;
    bool include_deps = true;
    std::vector<std::string> extra_paths;
    std::vector<std::string> exclude_patterns;
  };

  Result<int64_t> create_snapshot(int64_t project_id,
                                   const SnapshotOptions& opts);

  // Auto-snapshot before risky operations
  Result<int64_t> auto_snapshot(int64_t project_id,
                                 const std::string& trigger);

  // List snapshots
  std::vector<WorkspaceSnapshot> list_snapshots(int64_t project_id);

  // Get specific snapshot
  std::optional<WorkspaceSnapshot> get_snapshot(int64_t snapshot_id);
  std::optional<WorkspaceSnapshot> get_snapshot_by_tag(int64_t project_id,
                                                        const std::string& tag);

  // Tag management
  bool tag_snapshot(int64_t snapshot_id, const std::string& tag);
  bool untag(const std::string& tag);

  // Restore
  struct RestoreOptions {
    std::string restore_type = "smart"; // "smart", "full", "partial"
    bool dry_run = false;
    bool interactive = true;
    std::vector<std::string> include_paths;
    std::vector<std::string> exclude_paths;
  };

  Result<RestoreResult> restore(int64_t project_id,
                                 int64_t snapshot_id,
                                 const RestoreOptions& opts);

  // Diff snapshots
  Result<SnapshotDiff> diff(int64_t snapshot_a, int64_t snapshot_b);
  Result<SnapshotDiff> diff_current(int64_t project_id, int64_t snapshot_id);

  // Cleanup
  void prune_old_snapshots(int64_t project_id, int keep_count);
  void prune_untagged_snapshots(int64_t project_id, int days_old);

private:
  nexus::Store* store_;
  brain::Detector* detector_;
  blackbox::logger* log_;
};

} // namespace nazg::workspace
```

### WorkspaceSnapshot

```cpp
// modules/workspace/include/workspace/types.hpp

namespace nazg::workspace {

struct WorkspaceFile {
  std::string path;
  std::string type;  // "source", "build", "dep", "config"
  std::string hash;
  int64_t size;
  int64_t mtime;
};

struct WorkspaceSnapshot {
  int64_t id = 0;
  int64_t project_id = 0;
  int64_t brain_snapshot_id = 0;
  std::string label;
  std::string trigger_type;
  int64_t timestamp = 0;

  // Extended state
  std::string build_dir_hash;
  std::string deps_manifest_hash;
  std::map<std::string, std::string> env_snapshot;
  std::map<std::string, std::string> system_info;

  // Metadata
  int restore_count = 0;
  bool is_clean_build = false;
  std::string git_commit;
  std::string git_branch;
  std::vector<std::string> tags;

  // Files (populated separately)
  std::vector<WorkspaceFile> files;
};

struct RestoreResult {
  bool success;
  int files_restored;
  int files_skipped;
  std::vector<std::string> errors;
  int64_t duration_ms;
};

struct WorkspaceFailure {
  std::string failure_type;
  std::string error_signature;
  std::string error_message;
  int64_t before_snapshot_id;
  int64_t after_snapshot_id;
  std::vector<std::string> changed_files;
  std::map<std::string, std::string> changed_deps;
};

} // namespace nazg::workspace
```

### SnapshotDiffer

```cpp
// modules/workspace/include/workspace/differ.hpp

namespace nazg::workspace {

enum class ChangeType {
  ADDED,
  MODIFIED,
  DELETED,
  UNCHANGED
};

struct FileChange {
  std::string path;
  ChangeType change_type;
  std::string old_hash;
  std::string new_hash;
  int64_t size_diff;
};

struct SnapshotDiff {
  int64_t snapshot_a_id;
  int64_t snapshot_b_id;

  // File changes by category
  std::vector<FileChange> source_changes;
  std::vector<FileChange> build_changes;
  std::vector<FileChange> dep_changes;
  std::vector<FileChange> config_changes;

  // High-level changes
  std::map<std::string, std::string> dep_upgrades;   // "libfoo": "1.2 -> 1.3"
  std::map<std::string, std::string> env_changes;
  std::vector<std::string> git_commits_between;

  // Analysis
  std::vector<std::string> risk_factors;  // "Major version bump", "ABI change"
  std::vector<std::string> suggestions;   // Human-readable advice
};

class Differ {
public:
  Differ(nexus::Store* store, blackbox::logger* log);

  // Compare two snapshots
  SnapshotDiff diff(const WorkspaceSnapshot& a, const WorkspaceSnapshot& b);

  // Compare current state to snapshot
  SnapshotDiff diff_current(int64_t project_id, const WorkspaceSnapshot& snapshot);

  // Analyze what changed between snapshots
  std::vector<std::string> analyze_risk_factors(const SnapshotDiff& diff);
  std::vector<std::string> suggest_fixes(const SnapshotDiff& diff);

private:
  nexus::Store* store_;
  blackbox::logger* log_;

  std::string compute_error_signature(const std::string& error_msg);
  std::vector<WorkspaceFailure> find_similar_failures(
      int64_t project_id,
      const std::string& signature);
};

} // namespace nazg::workspace
```

### WorkspaceRestorer

```cpp
// modules/workspace/include/workspace/restorer.hpp

namespace nazg::workspace {

class Restorer {
public:
  Restorer(nexus::Store* store, blackbox::logger* log);

  // Full restore (everything)
  RestoreResult restore_full(int64_t project_id,
                              const WorkspaceSnapshot& snapshot,
                              bool interactive);

  // Smart restore (minimal changes)
  RestoreResult restore_smart(int64_t project_id,
                               const WorkspaceSnapshot& snapshot,
                               const SnapshotDiff& diff,
                               bool interactive);

  // Partial restore (specific paths)
  RestoreResult restore_partial(int64_t project_id,
                                 const WorkspaceSnapshot& snapshot,
                                 const std::vector<std::string>& paths);

  // Dry run (show what would be restored)
  std::vector<std::string> preview_restore(const WorkspaceSnapshot& snapshot,
                                            const SnapshotDiff& diff);

private:
  nexus::Store* store_;
  blackbox::logger* log_;

  bool restore_file(const WorkspaceFile& file, const std::string& project_root);
  bool backup_current_file(const std::string& path);
};

} // namespace nazg::workspace
```

## Command Specifications

### nazg workspace snapshot

Create a workspace snapshot (manual checkpoint).

```bash
# Basic snapshot
nazg workspace snapshot

# With label
nazg workspace snapshot --label "pre-refactor"

# With tag for easy reference
nazg workspace snapshot --tag working

# Exclude build directory
nazg workspace snapshot --no-build

# Include extra paths
nazg workspace snapshot --include .vscode --include config.local
```

**Options:**
- `--label <text>` - Human-readable label
- `--tag <name>` - Named tag for quick reference (e.g., "working", "v1.0")
- `--no-build` - Exclude build directory
- `--no-deps` - Don't snapshot dependency manifests
- `--include <path>` - Include additional paths
- `--exclude <pattern>` - Exclude paths matching pattern

### nazg workspace history

List workspace snapshots with timeline view.

```bash
# Show recent snapshots
nazg workspace history

# Show all snapshots
nazg workspace history --all

# Filter by tag
nazg workspace history --tagged

# Filter by trigger type
nazg workspace history --auto-only
```

**Output:**
```
Workspace Snapshots for myproject

ID   Age       Label              Tag       Trigger    Files  Status
───────────────────────────────────────────────────────────────────────
42   2m ago    current state      working   manual     156    ✓ clean
41   1h ago    pre-upgrade                  pre-build  152    ✓ clean
40   3h ago    before refactor    stable    manual     145    ✓ clean
39   1d ago    initial setup                auto       140    dirty

Use 'nazg workspace show <id>' for details
Use 'nazg workspace restore <id>' to restore
```

### nazg workspace show

Show detailed information about a snapshot.

```bash
# Show by ID
nazg workspace show 42

# Show by tag
nazg workspace show @working

# Show current state
nazg workspace show @current
```

**Output:**
```
Workspace Snapshot #42

Label:       current state
Tag:         working
Created:     2025-10-12 14:30:00 (2 minutes ago)
Trigger:     manual
Git:         main@a3f2c1b (clean)

Contents:
  Source files:       98 files  (2.4 MB)
  Build artifacts:    47 files  (5.1 MB)
  Dependencies:       11 files  (145 KB)
  Configuration:      3 files   (12 KB)
  Total:             159 files  (7.7 MB)

System State:
  Compiler:     g++ 13.2.1
  CMake:        3.27.0
  Build type:   RelWithDebInfo

Environment:
  CXX=g++
  BUILD_TYPE=RelWithDebInfo

Restored:      Never
```

### nazg workspace diff

Compare snapshots or current state.

```bash
# Compare current to a snapshot
nazg workspace diff @working

# Compare two snapshots
nazg workspace diff 40 42

# Compare to snapshot from time
nazg workspace diff @2-hours-ago

# Detailed diff
nazg workspace diff @working --verbose
```

**Output:**
```
Comparing current state → snapshot #42 (@working, 2m ago)

Source Changes:     5 files modified
Build Changes:      12 files modified, 3 added
Dependencies:       No changes
Configuration:      1 file modified

Modified Files:
  M  src/engine/runtime.cpp
  M  src/workspace/manager.cpp
  M  include/workspace/types.hpp
  M  CMakeLists.txt
  A  tests/workspace_tests.cpp

Risk Factors:
  ⚠ CMakeLists.txt modified (build system changes)

Suggestions:
  → Run 'cmake --build build' to rebuild
  → Run tests before committing
```

### nazg workspace restore

Restore workspace to a previous snapshot.

```bash
# Smart restore (interactive, minimal changes)
nazg workspace restore @working

# Full restore (everything)
nazg workspace restore @working --full

# Dry run (preview only)
nazg workspace restore @working --dry-run

# Restore specific files only
nazg workspace restore @working --only src/

# Non-interactive
nazg workspace restore 42 --yes
```

**Interactive Flow:**
```
Restoring from snapshot #42 (@working, 2m ago)

Analyzing changes...

The following will be restored:
  ✓ src/engine/runtime.cpp          (modified)
  ✓ src/workspace/manager.cpp       (modified)
  ✓ include/workspace/types.hpp     (modified)
  ✗ build/ directory                 (skip: rebuild recommended)
  ? CMakeLists.txt                   (modified - risky)

Restore CMakeLists.txt? [y/N/v(iew diff)] v

--- CMakeLists.txt (current)
+++ CMakeLists.txt (snapshot @working)
@@ -45,7 +45,6 @@
-add_nazg_module(workspace blackbox nexus brain system)
 add_nazg_module(bot system blackbox directive nexus config prompt agent)

Restore? [y/N] y

Proceed with restore? [y/N] y

Restoring 4 files... ✓
Skipping 12 build artifacts (run 'nazg build' to rebuild)

✓ Restored to snapshot #42
  Run 'nazg build' to rebuild
```

### nazg workspace tag

Manage snapshot tags.

```bash
# Tag current state
nazg workspace tag working

# Tag specific snapshot
nazg workspace tag 42 stable

# Tag with description
nazg workspace tag @latest "before big refactor"

# List tags
nazg workspace tag --list

# Remove tag
nazg workspace tag --delete working
```

### nazg workspace prune

Clean up old snapshots.

```bash
# Keep only last 10 snapshots
nazg workspace prune --keep 10

# Delete untagged snapshots older than 30 days
nazg workspace prune --days 30 --untagged-only

# Dry run
nazg workspace prune --keep 5 --dry-run
```

## Integration with Existing Modules

### Engine Integration

Auto-snapshot before risky operations:

```cpp
// modules/engine/src/runtime.cpp

// Before build
if (config->get_bool("workspace.auto_snapshot", true)) {
  workspace_mgr->auto_snapshot(project_id, "pre-build");
}

// Before git operations
if (cmd == "git-rebase" || cmd == "git-merge") {
  workspace_mgr->auto_snapshot(project_id, "pre-git-" + cmd);
}

// Before dependency upgrades
if (cmd == "upgrade-deps") {
  workspace_mgr->auto_snapshot(project_id, "pre-upgrade");
}
```

### Brain Integration

Extend snapshot system to include workspace files:

```cpp
// modules/brain/src/snapshot.cpp

// After creating brain snapshot, workspace manager extends it
auto ws_snapshot = workspace_mgr->extend_snapshot(brain_snapshot_id);
```

### System Integration

Use existing filesystem utilities:

```cpp
// modules/workspace/src/snapshot.cpp

#include "system/fs.hpp"

// Leverage existing file operations
system::copy_file(source, backup_path);
system::compute_hash(file_path);  // SHA256
system::list_files_recursive(dir, pattern);
```

## Configuration

Add to `~/.config/nazg/config.toml`:

```toml
[workspace]
# Auto-snapshot before risky operations
auto_snapshot = true

# Triggers that create auto-snapshots
auto_triggers = ["pre-build", "pre-upgrade", "pre-git-rebase"]

# Retention policy
keep_snapshots = 20
keep_tagged_forever = true
prune_untagged_days = 30

# What to include in snapshots
include_build_dir = true
include_deps = true
max_snapshot_size_mb = 500

# Exclude patterns (in addition to .gitignore)
exclude = [
  "*.o",
  "*.a",
  "*.so",
  "__pycache__",
  "node_modules",
  ".cache"
]

[workspace.restore]
# Default restore mode
default_mode = "smart"  # or "full"

# Interactive by default
interactive = true

# Backup current state before restore
backup_before_restore = true
```

## Testing Strategy

### Testsuite Module Enhancements

Update `modules/testsuite` to support workspace testing:

```cpp
// modules/testsuite/include/testsuite/runner.hpp

namespace nazg::testsuite {

class Runner {
public:
  // Existing test running methods...

  // NEW: Workspace test support
  struct WorkspaceTestContext {
    std::string temp_project_dir;
    int64_t project_id;
    nexus::Store* store;
    workspace::Manager* ws_mgr;

    // Helper methods for tests
    void create_file(const std::string& path, const std::string& content);
    void modify_file(const std::string& path);
    void delete_file(const std::string& path);
    int64_t create_snapshot(const std::string& label = "");
    void verify_snapshot_exists(int64_t snapshot_id);
    void verify_file_content(const std::string& path, const std::string& expected);
  };

  // Create isolated test workspace
  WorkspaceTestContext setup_workspace_test(const std::string& test_name);

  // Clean up test workspace
  void teardown_workspace_test(WorkspaceTestContext& ctx);

private:
  std::string temp_test_root_;
};

} // namespace nazg::testsuite
```

### Test Scenarios

```cpp
// tests/workspace_tests.cpp

#include "testsuite/runner.hpp"
#include "workspace/manager.hpp"

void test_basic_snapshot() {
  auto ctx = runner.setup_workspace_test("basic_snapshot");

  // Create some files
  ctx.create_file("src/main.cpp", "int main() {}");
  ctx.create_file("README.md", "Test project");

  // Create snapshot
  auto snapshot_id = ctx.create_snapshot("initial");
  ctx.verify_snapshot_exists(snapshot_id);

  runner.teardown_workspace_test(ctx);
}

void test_restore_modified_files() {
  auto ctx = runner.setup_workspace_test("restore_modified");

  // Create initial file
  ctx.create_file("src/main.cpp", "int main() { return 0; }");
  auto snapshot_id = ctx.create_snapshot("original");

  // Modify file
  ctx.modify_file("src/main.cpp");

  // Restore
  auto result = ctx.ws_mgr->restore(ctx.project_id, snapshot_id, {});
  assert(result.success);

  // Verify restoration
  ctx.verify_file_content("src/main.cpp", "int main() { return 0; }");

  runner.teardown_workspace_test(ctx);
}

void test_smart_restore() {
  auto ctx = setup_workspace_test("smart_restore");

  // Initial state
  ctx.create_file("src/good.cpp", "good code");
  ctx.create_file("src/bad.cpp", "bad code");
  auto snapshot_id = ctx.create_snapshot("working");

  // Break one file, improve another
  ctx.modify_file("src/bad.cpp");  // This breaks build
  ctx.modify_file("src/good.cpp"); // This is a good change

  // Smart restore should only restore bad.cpp
  workspace::Manager::RestoreOptions opts;
  opts.restore_type = "smart";
  opts.interactive = false;

  auto result = ctx.ws_mgr->restore(ctx.project_id, snapshot_id, opts);

  // bad.cpp restored, good.cpp preserved
  ctx.verify_file_content("src/bad.cpp", "bad code");
  assert(result.files_restored == 1);

  teardown_workspace_test(ctx);
}
```

### Testing Commands

```bash
# Run workspace tests specifically
nazg test --suite workspace

# Run with verbose output
nazg test --suite workspace --verbose

# Test a specific scenario
nazg test workspace::test_smart_restore
```

## Implementation Phases

### Phase 1: Foundation (Week 1)
- [ ] Create workspace module structure
- [ ] Add database schema to nexus migrations
- [ ] Implement basic WorkspaceSnapshot type
- [ ] Extend nexus::Store with workspace methods
- [ ] Basic snapshot creation (no build artifacts yet)
- [ ] Command: `nazg workspace snapshot`
- [ ] Command: `nazg workspace history`

### Phase 2: Enhanced Snapshot (Week 2)
- [ ] Capture build directory state
- [ ] Capture dependency manifests
- [ ] Capture environment variables
- [ ] Capture system info (compiler versions)
- [ ] File type detection (source vs build vs config)
- [ ] Implement snapshot pruning
- [ ] Command: `nazg workspace show`
- [ ] Command: `nazg workspace prune`

### Phase 3: Restore (Week 3)
- [ ] Implement full restore
- [ ] Implement dry-run mode
- [ ] Add backup-before-restore safety
- [ ] Interactive restore prompts (using prompt module)
- [ ] Command: `nazg workspace restore`
- [ ] Command: `nazg workspace restore --dry-run`

### Phase 4: Diff & Smart Restore (Week 4)
- [ ] Implement SnapshotDiffer
- [ ] File-level diff computation
- [ ] Dependency change detection
- [ ] Risk factor analysis
- [ ] Smart restore algorithm
- [ ] Command: `nazg workspace diff`
- [ ] Command: `nazg workspace restore --smart`

### Phase 5: Tagging & Auto-Snapshot (Week 5)
- [ ] Implement snapshot tagging
- [ ] Tag resolution (@working, @stable)
- [ ] Engine integration for auto-snapshots
- [ ] Pre-build snapshots
- [ ] Pre-git-operation snapshots
- [ ] Configuration support
- [ ] Command: `nazg workspace tag`

### Phase 6: Failure Learning (Week 6)
- [ ] Implement failure pattern recording
- [ ] Error signature computation
- [ ] Similar failure detection
- [ ] Suggestion engine
- [ ] Integration with build failures
- [ ] Integration with test failures

### Phase 7: Testing (Week 7)
- [ ] Enhance testsuite module for workspace tests
- [ ] Implement WorkspaceTestContext helpers
- [ ] Write test scenarios
- [ ] Add tests to smoke test suite
- [ ] Documentation examples
- [ ] Integration tests with other modules

### Phase 8: Polish (Week 8)
- [ ] Performance optimization (parallel hashing)
- [ ] Compression for stored snapshots
- [ ] Better progress indicators
- [ ] Improved interactive prompts
- [ ] Better error messages
- [ ] Documentation updates
- [ ] User guide with examples

## Usage Examples

### Example 1: Safe Dependency Upgrade

```bash
# Working on a C++ project
cd myproject

# Everything works, tag current state
nazg workspace tag working

# Upgrade a dependency
# (nazg auto-creates snapshot before upgrade)
nazg upgrade boost

# Build fails!
nazg build
# Error: undefined reference to boost::...

# See what changed
nazg workspace diff @working
# Shows: boost 1.81 → 1.82, API changes detected

# Restore to working state
nazg workspace restore @working --smart
# Restores only dependency changes, keeps your code changes

# Back to working state
nazg build  # ✓ Success
```

### Example 2: Experimental Refactoring

```bash
# Tag before major refactor
nazg workspace snapshot --tag pre-refactor \
  --label "Stable before architecture change"

# Make sweeping changes...
# Hours later: everything is broken

# See timeline
nazg workspace history
# 52  2h ago   Stable before...  pre-refactor
# 53  1h ago   auto-snapshot     pre-build
# 54  now      current state

# Compare damage
nazg workspace diff @pre-refactor
# 47 files modified, build system changed

# Just give up and restore
nazg workspace restore @pre-refactor --full
# Back to working state in seconds
```

### Example 3: CI/CD Integration

```bash
# In CI pipeline, create tagged snapshot after successful builds
if nazg test; then
  nazg workspace snapshot --tag "ci-build-${BUILD_NUMBER}"
fi

# Developers can restore to any CI build
nazg workspace restore @ci-build-1234
```

### Example 4: Team Collaboration

```bash
# Developer A tags known-good state
nazg workspace snapshot --tag team-baseline
# Exports snapshot metadata (git commit, deps, etc)
nazg workspace show @team-baseline > workspace-baseline.txt
git add workspace-baseline.txt && git commit -m "Workspace baseline"

# Developer B on different machine
git pull
# Sees workspace-baseline.txt in repo
# Can verify their workspace matches
nazg workspace diff @team-baseline
```

## Success Metrics

After implementation, developers should experience:

1. **Time Savings**
   - Average 2+ hours saved per week on "what changed?" debugging
   - Instant rollback vs 30+ minutes manual restoration
   - Fearless experimentation (no hesitation to try risky changes)

2. **Reduced Risk**
   - Zero lost work due to failed experiments
   - Automatic snapshots prevent "I wish I had backed up" moments
   - Clear audit trail of what changed when

3. **Better Understanding**
   - Diff shows exactly what broke
   - Failure patterns identify common issues
   - Smart restore suggests minimal fixes

4. **Workflow Integration**
   - Auto-snapshots become invisible safety net
   - Tagged snapshots become team communication tool
   - Workspace history becomes debugging tool

## Future Enhancements

Post-MVP features to consider:

1. **Remote Snapshots**
   - Push snapshots to remote storage
   - Share snapshots with team
   - Fetch teammate's working state

2. **Intelligent Scheduling**
   - Learn user patterns, create snapshots proactively
   - "You usually break things on Friday, extra snapshot?"

3. **Build Cache Integration**
   - Restore not just to buildable state, but built state
   - Instant binary restoration for known-good builds

4. **Git Integration**
   - Workspace snapshots linked to git commits
   - Restore "git commit + build state" atomically

5. **Cloud Backup**
   - Automatic upload of tagged snapshots
   - Disaster recovery for entire workspace

6. **Visual Timeline**
   - TUI-based interactive timeline browser
   - Visual diff view with syntax highlighting

7. **Workspace Templates**
   - Save workspace configs as templates
   - Quick setup for new contributors
   - "Clone and workspace-restore in one command"

## Conclusion

The Workspace Time Machine transforms Nazg from a helpful assistant into an essential safety net. By combining intelligent snapshot management with Nazg's existing project understanding, developers gain:

- **Fearless experimentation** - Try anything, restore in seconds
- **Faster debugging** - Clear view of what changed and why
- **Team collaboration** - Share known-good states easily
- **Learning system** - Nazg learns from failures to suggest fixes

This feature leverages Nazg's unique position (project-aware, persistent, intelligent) to solve a problem that no other tool addresses well: complete, lightweight, intelligent workspace state management.

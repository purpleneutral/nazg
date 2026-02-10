# Git Module Enhancement Plan

**Version:** 1.0
**Created:** 2025-10-06
**Status:** Planning Phase

---

## Executive Summary

This document outlines a comprehensive plan to enhance Nazg's Git module with intelligent, AI-powered features that go beyond basic version control operations. The goal is to transform the git module from a simple command wrapper into an intelligent assistant that:

1. **Generates conventional commit messages** from diff analysis
2. **Detects and adapts to git workflows** (gitflow, trunk-based, feature branches)
3. **Provides pre-commit quality gates** (linting, formatting, tests)
4. **Suggests intelligent conflict resolution** strategies
5. **Recommends branching strategies** based on project patterns
6. **Integrates with git platforms** (GitHub, GitLab) for PR/MR workflows

---

## Table of Contents

1. [Current Capabilities Analysis](#1-current-capabilities-analysis)
2. [Identified Gaps](#2-identified-gaps)
3. [Enhancement Goals](#3-enhancement-goals)
4. [Architecture Overview](#4-architecture-overview)
5. [Feature Specifications](#5-feature-specifications)
6. [Database Schema Changes](#6-database-schema-changes)
7. [Module Integration](#7-module-integration)
8. [Implementation Phases](#8-implementation-phases)
9. [Testing Strategy](#9-testing-strategy)
10. [Security Considerations](#10-security-considerations)

---

## 1. Current Capabilities Analysis

### 1.1 Existing Features

**Core Client (`git::Client`)**:
- Repository detection and initialization
- Status parsing (porcelain v2 format)
- Basic operations: add, commit, push
- Config management (global/local)
- Remote management (add/remove/list)
- Identity management (ensure_identity)
- Branch tracking (ahead/behind counts)

**Maintenance (`git::Maintenance`)**:
- Language-specific .gitignore generation (C++, Python, Rust, Go, JS)
- .gitattributes templates
- Initial commit creation

**Bare Repository Management (`git::BareManager`)**:
- Create bare repositories
- Link working trees to bare repos
- Clone from bare repositories

**Server Integration**:
- cgit server management
- Remote server installation
- Repository syncing to servers

**CLI Commands**:
- `git-init` - Interactive repo initialization
- `git-status` - Detailed status display
- `git-config` - Interactive configuration
- `git-commit` - Quick commit workflow
- `git-sync` - Push to all remotes
- `git-server-*` - Server management commands

### 1.2 Strengths

1. **Safe command construction** - Uses `shell_quote` to prevent injection
2. **Rich status information** - Parses porcelain v2 for accurate state
3. **Logger integration** - All operations logged through blackbox
4. **Interactive prompts** - Uses prompt module for confirmations
5. **Language awareness** - Generates appropriate .gitignore per language
6. **Bare repo support** - Good foundation for advanced workflows

### 1.3 Integration Points

- **Brain Module**: Used for language detection during `git-init`
- **Nexus**: Could store git history, commit patterns, workflow preferences
- **Prompt Module**: Used for interactive confirmations
- **System Module**: Uses `shell_quote` for safe command execution
- **Blackbox**: Comprehensive logging of all git operations

---

## 2. Identified Gaps

### 2.1 Intelligence Gaps

| Gap | Impact | Priority |
|-----|--------|----------|
| No AI-generated commit messages | Users write manual, often non-descriptive commits | **HIGH** |
| No workflow detection | Cannot adapt suggestions to team practices | **HIGH** |
| No diff analysis | Cannot suggest semantic changes or improvements | **MEDIUM** |
| No commit history patterns | Cannot learn from project's commit conventions | **MEDIUM** |
| No branch naming suggestions | Inconsistent branch names across team | **LOW** |

### 2.2 Quality Gates Gaps

| Gap | Impact | Priority |
|-----|--------|----------|
| No pre-commit hooks | Code quality issues reach repository | **HIGH** |
| No format checking | Inconsistent code style | **MEDIUM** |
| No test execution | Broken code gets committed | **HIGH** |
| No lint integration | Style violations accumulate | **MEDIUM** |
| No secret detection | Credentials may leak into history | **HIGH** |

### 2.3 Workflow Gaps

| Gap | Impact | Priority |
|-----|--------|----------|
| No conflict resolution help | Difficult merges block progress | **MEDIUM** |
| No rebase assistance | Complex rebases error-prone | **LOW** |
| No stash management | WIP changes lost or disorganized | **LOW** |
| No interactive staging | Cannot stage hunks selectively | **MEDIUM** |
| No cherry-pick helpers | Difficult to backport fixes | **LOW** |

### 2.4 Platform Integration Gaps

| Gap | Impact | Priority |
|-----|--------|----------|
| No GitHub integration | Manual PR creation | **MEDIUM** |
| No GitLab integration | No MR workflow | **MEDIUM** |
| No PR template generation | Inconsistent PR descriptions | **LOW** |
| No issue linking | Commits not linked to issues | **LOW** |
| No CI/CD awareness | Cannot check build status | **LOW** |

---

## 3. Enhancement Goals

### 3.1 Primary Goals

1. **Intelligent Commit Messages** (Phase 1)
   - Analyze staged diffs to generate conventional commit messages
   - Support multiple conventions (conventional commits, semantic commits, custom)
   - Learn from project's commit history patterns
   - Suggest scope based on changed files

2. **Pre-Commit Quality Gates** (Phase 1)
   - Integrate with brain module to run appropriate linters
   - Execute tests before allowing commit
   - Check for secrets/credentials in staged changes
   - Validate commit message format
   - Format code automatically if configured

3. **Workflow Detection** (Phase 2)
   - Detect gitflow, trunk-based, GitHub flow, GitLab flow
   - Suggest appropriate branch names based on workflow
   - Validate branch naming conventions
   - Recommend merge vs rebase based on workflow

4. **Smart Branching** (Phase 2)
   - Suggest branch names from issue/task descriptions
   - Validate branch naming conventions
   - Detect stale branches and suggest cleanup
   - Recommend branch strategy based on team size

### 3.2 Secondary Goals

5. **Conflict Resolution Assistant** (Phase 3)
   - Analyze merge conflicts and suggest resolution strategies
   - Show conflict context (both sides with history)
   - Offer "take ours/theirs" for specific files
   - Suggest using mergetool for complex conflicts

6. **Platform Integration** (Phase 3)
   - GitHub: Create PRs with templates, link issues, check CI
   - GitLab: Create MRs, assign reviewers, check pipelines
   - Generate PR/MR descriptions from commit messages
   - Link commits to issues automatically

7. **History Analysis** (Phase 4)
   - Analyze commit patterns over time
   - Detect commit message conventions used
   - Identify frequent contributors per file/module
   - Suggest code reviewers based on file ownership

---

## 4. Architecture Overview

### 4.1 New Components

```
modules/git/
├── include/git/
│   ├── analyzer.hpp          # NEW: Diff analysis and commit message generation
│   ├── workflow.hpp           # NEW: Workflow detection and suggestions
│   ├── quality.hpp            # NEW: Pre-commit quality gates
│   ├── hooks.hpp              # NEW: Git hooks management
│   ├── platform.hpp           # NEW: GitHub/GitLab integration base
│   ├── github.hpp             # NEW: GitHub-specific API
│   └── gitlab.hpp             # NEW: GitLab-specific API
├── src/
│   ├── analyzer.cpp           # Diff parsing and message generation
│   ├── workflow.cpp           # Workflow pattern detection
│   ├── quality.cpp            # Quality gate execution
│   ├── hooks.cpp              # Hook installation and management
│   ├── platform.cpp           # Platform integration base
│   ├── github.cpp             # GitHub API client
│   └── gitlab.cpp             # GitLab API client
```

### 4.2 Data Flow

```
User: nazg commit
    ↓
Engine: git-commit command
    ↓
Git Module:
    1. Status check (Client)
    2. Diff analysis (Analyzer) → Generate commit message
    3. Quality gates (Quality) → Run linters, tests, secret scan
    4. Workflow check (Workflow) → Validate branch name, suggest next steps
    5. Commit creation (Client)
    6. Store commit metadata (Nexus)
    7. Platform sync (Platform) → Link to issues, update PR
```

### 4.3 Dependencies

- **External**: `libcurl` (for GitHub/GitLab API), `libgit2` (optional, for better performance)
- **Internal**: brain (for language detection), nexus (for pattern storage), system (for process execution)

---

## 5. Feature Specifications

### 5.1 Intelligent Commit Messages (`git::Analyzer`)

#### Purpose
Generate high-quality commit messages by analyzing staged diffs and learning from project history.

#### API Design

```cpp
namespace nazg::git {

enum class CommitConvention {
  CONVENTIONAL,      // feat: description
  SEMANTIC,          // type(scope): description
  SIMPLE,            // Simple present tense
  CUSTOM             // User-defined pattern
};

struct DiffAnalysis {
  std::vector<std::string> added_files;
  std::vector<std::string> modified_files;
  std::vector<std::string> deleted_files;
  std::map<std::string, int> lines_added_per_file;
  std::map<std::string, int> lines_removed_per_file;
  std::vector<std::string> affected_modules;  // Based on directory structure
  std::string dominant_language;
  bool has_tests;
  bool has_docs;
};

struct CommitSuggestion {
  std::string type;           // feat, fix, docs, style, refactor, test, chore
  std::string scope;          // module/component affected
  std::string subject;        // Short description
  std::string body;           // Detailed description (optional)
  float confidence;           // 0.0-1.0
  std::vector<std::string> alternative_types;
};

class Analyzer {
public:
  Analyzer(nazg::nexus::Store *store, nazg::blackbox::logger *log);

  // Analyze staged changes
  DiffAnalysis analyze_diff(const std::string &repo_path);

  // Generate commit message suggestions
  CommitSuggestion suggest_commit_message(const DiffAnalysis &analysis,
                                          CommitConvention convention);

  // Learn from project's commit history
  void learn_from_history(const std::string &repo_path, int commit_count = 100);

  // Get detected convention from history
  CommitConvention detect_convention(const std::string &repo_path);

  // Validate commit message against detected convention
  bool validate_message(const std::string &message, CommitConvention convention);

private:
  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;

  std::string infer_type(const DiffAnalysis &analysis);
  std::string infer_scope(const DiffAnalysis &analysis);
  std::string generate_subject(const DiffAnalysis &analysis,
                                const std::string &type);
};

} // namespace nazg::git
```

#### Algorithm: Commit Type Inference

```
if added_files contains test files:
  type = "test"
else if only docs changed:
  type = "docs"
else if only formatting changed (no logic):
  type = "style"
else if deleted code > added code:
  type = "refactor"
else if changed files in core modules AND tests exist:
  type = "feat"
else if changed files affect single module:
  type = "fix"
else:
  type = "chore"
```

#### Algorithm: Scope Inference

```
affected_modules = unique top-level directories of changed files
if affected_modules.size() == 1:
  scope = affected_modules[0]
else if affected_modules.size() <= 3:
  scope = join(affected_modules, ",")
else:
  scope = "*" or ""  // Multiple modules
```

#### Example Usage

```cpp
git::Analyzer analyzer(store, log);

// Learn from history first
analyzer.learn_from_history(cwd, 100);
auto convention = analyzer.detect_convention(cwd);

// Analyze current diff
auto analysis = analyzer.analyze_diff(cwd);

// Generate suggestion
auto suggestion = analyzer.suggest_commit_message(analysis, convention);

// Present to user
std::cout << "Suggested commit message:\n";
std::cout << suggestion.type;
if (!suggestion.scope.empty()) {
  std::cout << "(" << suggestion.scope << ")";
}
std::cout << ": " << suggestion.subject << "\n";
std::cout << "Confidence: " << (suggestion.confidence * 100) << "%\n";
```

#### Database Schema

```sql
-- Migration: 005_git_patterns.sql
CREATE TABLE git_commit_patterns (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL,
  commit_hash TEXT NOT NULL,
  commit_type TEXT,           -- feat, fix, docs, etc.
  commit_scope TEXT,          -- module/component
  commit_subject TEXT,
  files_changed INTEGER,
  insertions INTEGER,
  deletions INTEGER,
  timestamp INTEGER NOT NULL,
  FOREIGN KEY (project_id) REFERENCES projects(id)
);

CREATE INDEX idx_git_patterns_project ON git_commit_patterns(project_id);
CREATE INDEX idx_git_patterns_type ON git_commit_patterns(commit_type);
```

---

### 5.2 Pre-Commit Quality Gates (`git::Quality`)

#### Purpose
Run automated checks before allowing commits to ensure code quality and prevent common issues.

#### API Design

```cpp
namespace nazg::git {

enum class QualityGate {
  LINT,              // Run linter (configured per language)
  FORMAT,            // Check code formatting
  TEST,              // Run tests
  SECRET_SCAN,       // Check for leaked credentials
  MESSAGE_FORMAT,    // Validate commit message
  BUILD,             // Ensure code builds
  COVERAGE           // Check test coverage threshold
};

struct QualityResult {
  QualityGate gate;
  bool passed;
  std::string output;
  std::vector<std::string> errors;
  int exit_code;
};

struct QualityConfig {
  std::set<QualityGate> enabled_gates;
  bool auto_fix = false;       // Auto-fix formatting issues
  bool block_on_failure = true; // Prevent commit if gates fail
  int coverage_threshold = 0;  // Minimum coverage % (0 = disabled)
};

class Quality {
public:
  Quality(nazg::nexus::Store *store, nazg::blackbox::logger *log);

  // Run all configured quality gates
  std::vector<QualityResult> run_gates(const std::string &repo_path,
                                       const QualityConfig &config);

  // Run specific gate
  QualityResult run_gate(QualityGate gate, const std::string &repo_path);

  // Auto-fix issues if possible
  bool auto_fix(const std::string &repo_path, QualityGate gate);

  // Get recommended gates for project
  QualityConfig recommend_config(const std::string &repo_path);

private:
  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;

  QualityResult run_linter(const std::string &repo_path);
  QualityResult run_formatter(const std::string &repo_path);
  QualityResult run_tests(const std::string &repo_path);
  QualityResult scan_secrets(const std::string &repo_path);
  QualityResult check_message_format(const std::string &message);
};

} // namespace nazg::git
```

#### Gate Implementation Details

**LINT**: Uses brain module to detect language, then runs:
- C++: `clang-tidy`
- Python: `pylint` or `ruff`
- Rust: `cargo clippy`
- Go: `golint`
- JavaScript: `eslint`

**FORMAT**: Checks formatting without modifying:
- C++: `clang-format --dry-run`
- Python: `black --check`
- Rust: `cargo fmt --check`
- Go: `gofmt -l`

**TEST**: Integrates with test module:
```cpp
// Use test runner to execute tests
nazg::test::Runner runner(store_, log_);
auto result = runner.execute(project_id, plan, opts);
return result.failed == 0 && result.errors == 0;
```

**SECRET_SCAN**: Regex patterns for common secrets:
- AWS keys: `AKIA[0-9A-Z]{16}`
- GitHub tokens: `ghp_[a-zA-Z0-9]{36}`
- Private keys: `-----BEGIN (RSA|OPENSSH) PRIVATE KEY-----`
- Generic passwords: `password\s*=\s*['"][^'"]{8,}`

#### Hook Installation

```cpp
// Install pre-commit hook
bool Quality::install_hook(const std::string &repo_path) {
  std::string hook_path = repo_path + "/.git/hooks/pre-commit";
  std::ofstream hook(hook_path);
  hook << "#!/bin/sh\n";
  hook << "# Nazg pre-commit quality gates\n";
  hook << "nazg git-quality-check --block\n";
  hook << "exit $?\n";

  // Make executable
  fs::permissions(hook_path, fs::perms::owner_exec | fs::perms::owner_read);
  return true;
}
```

#### Example Usage

```cpp
git::Quality quality(store, log);

// Get recommended config
auto config = quality.recommend_config(cwd);

// Run gates
auto results = quality.run_gates(cwd, config);

// Check if all passed
bool all_passed = true;
for (const auto &result : results) {
  if (!result.passed) {
    std::cerr << "FAILED: " << gate_name(result.gate) << "\n";
    std::cerr << result.output << "\n";
    all_passed = false;
  }
}

if (!all_passed && config.block_on_failure) {
  std::cerr << "Commit blocked by quality gates\n";
  return 1;
}
```

---

### 5.3 Workflow Detection (`git::Workflow`)

#### Purpose
Detect the team's git workflow and provide context-aware suggestions for branching, merging, and releases.

#### API Design

```cpp
namespace nazg::git {

enum class WorkflowType {
  UNKNOWN,
  GITFLOW,           // main + develop + feature/* + hotfix/* + release/*
  GITHUB_FLOW,       // main + feature branches, PR-based
  GITLAB_FLOW,       // main + environment branches
  TRUNK_BASED,       // main only, short-lived feature branches
  CUSTOM
};

struct WorkflowPattern {
  WorkflowType type;
  std::string main_branch;           // "main" or "master"
  std::optional<std::string> dev_branch;  // "develop" for gitflow
  std::vector<std::string> branch_prefixes;  // "feature/", "hotfix/", etc.
  bool uses_tags;
  bool uses_prs;
  int avg_branch_lifetime_days;
  float confidence;                  // 0.0-1.0
};

struct BranchSuggestion {
  std::string name;
  std::string type;                  // feature, hotfix, release, etc.
  std::string base_branch;           // Branch to create from
  std::string merge_target;          // Where to merge back to
  std::string rationale;
};

class Workflow {
public:
  Workflow(nazg::nexus::Store *store, nazg::blackbox::logger *log);

  // Detect workflow from branch patterns and history
  WorkflowPattern detect(const std::string &repo_path);

  // Suggest branch name based on workflow and task description
  BranchSuggestion suggest_branch(const WorkflowPattern &pattern,
                                  const std::string &task_description);

  // Validate branch name against workflow conventions
  bool validate_branch_name(const std::string &branch_name,
                           const WorkflowPattern &pattern);

  // Suggest next action based on current branch and workflow
  std::string suggest_next_action(const std::string &current_branch,
                                 const WorkflowPattern &pattern);

  // Detect stale branches
  std::vector<std::string> detect_stale_branches(const std::string &repo_path,
                                                 int days_threshold = 30);

private:
  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;

  WorkflowType classify_workflow(const std::vector<std::string> &branches);
  std::string sanitize_branch_name(const std::string &description);
};

} // namespace nazg::git
```

#### Detection Algorithm

```
1. Get all branches (local + remote)
2. Analyze patterns:
   - Count branches matching "feature/*", "hotfix/*", "release/*"
   - Check for "develop" or "development" branch
   - Count branches per prefix
   - Calculate average branch lifetime

3. Classify:
   if has "develop" AND has "feature/" AND has "hotfix/":
     → GITFLOW (confidence based on adherence)
   else if main-only with many short-lived branches:
     → TRUNK_BASED
   else if has "feature/" AND many PRs:
     → GITHUB_FLOW
   else if has environment branches (staging, production):
     → GITLAB_FLOW
   else:
     → CUSTOM or UNKNOWN
```

#### Example Usage

```cpp
git::Workflow workflow(store, log);

// Detect current workflow
auto pattern = workflow.detect(cwd);

std::cout << "Detected workflow: " << workflow_name(pattern.type) << "\n";
std::cout << "Main branch: " << pattern.main_branch << "\n";
std::cout << "Confidence: " << (pattern.confidence * 100) << "%\n";

// Suggest branch for new feature
auto suggestion = workflow.suggest_branch(pattern, "Add user authentication");
std::cout << "Suggested branch: " << suggestion.name << "\n";
std::cout << "Create from: " << suggestion.base_branch << "\n";
std::cout << "Merge to: " << suggestion.merge_target << "\n";

// Check for stale branches
auto stale = workflow.detect_stale_branches(cwd, 30);
if (!stale.empty()) {
  std::cout << "Stale branches (>30 days):\n";
  for (const auto &branch : stale) {
    std::cout << "  - " << branch << "\n";
  }
}
```

---

### 5.4 Platform Integration (`git::Platform`)

#### Purpose
Integrate with GitHub and GitLab APIs for PR/MR creation, issue linking, and CI/CD status checks.

#### API Design

```cpp
namespace nazg::git {

enum class PlatformType {
  UNKNOWN,
  GITHUB,
  GITLAB,
  GITEA,
  BITBUCKET
};

struct PRTemplate {
  std::string title;
  std::string description;
  std::vector<std::string> labels;
  std::vector<std::string> reviewers;
  std::optional<std::string> milestone;
  bool draft = false;
};

struct PRStatus {
  int number;
  std::string state;          // open, merged, closed
  std::string url;
  bool ci_passing;
  int approvals;
  int required_approvals;
  std::vector<std::string> failing_checks;
};

class Platform {
public:
  virtual ~Platform() = default;

  // Create PR/MR from current branch
  virtual PRStatus create_pr(const std::string &repo_path,
                            const PRTemplate &tmpl) = 0;

  // Get PR status for current branch
  virtual std::optional<PRStatus> get_pr_status(const std::string &repo_path) = 0;

  // Link commits to issues
  virtual bool link_to_issue(const std::string &commit_hash,
                            const std::string &issue_id) = 0;

  // Check CI/CD status
  virtual bool check_ci(const std::string &branch) = 0;

  // Generate PR template from commits
  virtual PRTemplate generate_pr_template(const std::string &repo_path,
                                         const std::string &base_branch) = 0;
};

// Factory function
std::unique_ptr<Platform> create_platform(PlatformType type,
                                         const std::string &api_token,
                                         nazg::blackbox::logger *log);

} // namespace nazg::git
```

#### GitHub Implementation

```cpp
class GitHubPlatform : public Platform {
public:
  GitHubPlatform(const std::string &api_token, nazg::blackbox::logger *log);

  PRStatus create_pr(const std::string &repo_path,
                    const PRTemplate &tmpl) override;

  std::optional<PRStatus> get_pr_status(const std::string &repo_path) override;

private:
  std::string api_token_;
  nazg::blackbox::logger *log_;

  // HTTP helpers
  std::string api_request(const std::string &endpoint,
                         const std::string &method,
                         const std::string &body);

  // Parse owner/repo from remote URL
  std::pair<std::string, std::string> parse_repo(const std::string &remote_url);
};
```

#### Example: PR Creation Flow

```cpp
// User runs: nazg git-pr "Add user auth"
// 1. Detect platform from remote URL
auto remote_url = client.get_origin();
auto platform_type = detect_platform_type(*remote_url);

// 2. Create platform client
auto platform = create_platform(platform_type, api_token, log);

// 3. Generate PR template from commits
auto tmpl = platform->generate_pr_template(cwd, "main");

// 4. Enhance with commit messages
auto commits = get_commits_since_base(cwd, "main");
tmpl.description = format_commits_as_changelog(commits);

// 5. Prompt user for confirmation
prompt::Prompt pr_prompt(log);
pr_prompt.title("Create Pull Request")
         .fact("Title", tmpl.title)
         .fact("Base", "main")
         .detail("Description:\n" + tmpl.description);

if (pr_prompt.confirm()) {
  auto status = platform->create_pr(cwd, tmpl);
  std::cout << "✓ PR created: " << status.url << "\n";
}
```

---

## 6. Database Schema Changes

### 6.1 Migration 005: Git Patterns

```sql
-- Track commit patterns for learning
CREATE TABLE git_commit_patterns (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL,
  commit_hash TEXT NOT NULL,
  commit_type TEXT,           -- feat, fix, docs, etc.
  commit_scope TEXT,          -- module/component
  commit_subject TEXT,
  files_changed INTEGER,
  insertions INTEGER,
  deletions INTEGER,
  timestamp INTEGER NOT NULL,
  FOREIGN KEY (project_id) REFERENCES projects(id)
);

CREATE INDEX idx_git_patterns_project ON git_commit_patterns(project_id);
CREATE INDEX idx_git_patterns_type ON git_commit_patterns(commit_type);
CREATE INDEX idx_git_patterns_timestamp ON git_commit_patterns(timestamp);
```

### 6.2 Migration 006: Git Workflows

```sql
-- Store detected workflow patterns
CREATE TABLE git_workflows (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL UNIQUE,
  workflow_type TEXT NOT NULL,  -- gitflow, github_flow, trunk_based, etc.
  main_branch TEXT,
  dev_branch TEXT,
  branch_prefixes TEXT,         -- JSON array: ["feature/", "hotfix/"]
  uses_tags INTEGER DEFAULT 0,
  detected_at INTEGER NOT NULL,
  confidence REAL,
  FOREIGN KEY (project_id) REFERENCES projects(id)
);

-- Track branch lifecycle
CREATE TABLE git_branches (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL,
  branch_name TEXT NOT NULL,
  branch_type TEXT,             -- feature, hotfix, release, etc.
  created_at INTEGER,
  merged_at INTEGER,
  deleted_at INTEGER,
  base_branch TEXT,
  FOREIGN KEY (project_id) REFERENCES projects(id)
);

CREATE INDEX idx_branches_project ON git_branches(project_id);
CREATE INDEX idx_branches_active ON git_branches(project_id, deleted_at)
  WHERE deleted_at IS NULL;
```

### 6.3 Migration 007: Platform Integration

```sql
-- Store platform credentials and settings
CREATE TABLE git_platforms (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL,
  platform_type TEXT NOT NULL,  -- github, gitlab, gitea
  remote_url TEXT NOT NULL,
  api_token TEXT,               -- Encrypted
  api_endpoint TEXT,            -- Custom endpoints (self-hosted)
  owner TEXT,                   -- GitHub: owner, GitLab: namespace
  repo_name TEXT,
  FOREIGN KEY (project_id) REFERENCES projects(id)
);

-- Track PR/MR metadata
CREATE TABLE git_pull_requests (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL,
  platform_id INTEGER NOT NULL,
  pr_number INTEGER,
  branch_name TEXT,
  base_branch TEXT,
  title TEXT,
  state TEXT,                   -- open, merged, closed
  url TEXT,
  created_at INTEGER,
  merged_at INTEGER,
  FOREIGN KEY (project_id) REFERENCES projects(id),
  FOREIGN KEY (platform_id) REFERENCES git_platforms(id)
);
```

---

## 7. Module Integration

### 7.1 Integration with Brain Module

**Current**: Brain detects language during `git-init` for .gitignore generation

**Enhanced**:
- Brain provides language info to Quality module for selecting linters
- Brain's Planner considers git status when suggesting actions
- Brain's Detector checks for git hooks and suggests installation

```cpp
// In brain::Planner::decide()
if (git_status.modified > 0 && git_status.staged > 0) {
  plan.action = Action::GIT_COMMIT;
  plan.reason = "You have staged changes ready to commit";

  // Suggest running quality gates
  if (!git_hooks_installed) {
    plan.suggestions.push_back("Install pre-commit hooks: nazg git-hooks-install");
  }
}
```

### 7.2 Integration with Test Module

**Current**: No integration

**Enhanced**:
- Quality gates invoke test runner before commits
- Test failures block commits if configured
- Test coverage changes tracked in commit metadata

```cpp
// In git::Quality::run_tests()
nazg::test::Runner runner(store_, log_);
nazg::test::RunOptions opts;
opts.fail_fast = true;
opts.timeout_seconds = 120;

auto result = runner.execute(project_id, plan, opts);
return QualityResult{
  .gate = QualityGate::TEST,
  .passed = (result.failed == 0 && result.errors == 0),
  .output = format_test_summary(result),
  .exit_code = result.exit_code
};
```

### 7.3 Integration with Nexus

**Current**: Limited git data stored

**Enhanced**:
- Store commit patterns for machine learning
- Track workflow detection results
- Store PR/MR metadata for history
- Cache platform API responses

### 7.4 Integration with Engine

**Current**: Commands registered in runtime

**Enhanced**:
- Engine suggests git actions in update loop
- Status display includes git workflow hints
- Interactive commit flow with message generation

---

## 8. Implementation Phases

### Phase 1: Intelligence Foundation (Weeks 1-3)

**Goal**: Add AI-powered commit messages and basic quality gates

**Tasks**:
1. ✅ Create `modules/git/include/git/analyzer.hpp`
2. ✅ Implement `git::Analyzer` class
   - Diff parsing logic
   - Type inference algorithm
   - Scope detection
   - Message generation
3. ✅ Create `modules/git/include/git/quality.hpp`
4. ✅ Implement `git::Quality` class
   - Linter integration (clang-tidy, pylint, clippy)
   - Format checker (clang-format, black, rustfmt)
   - Secret scanner (regex patterns)
5. ✅ Add database migration `005_git_patterns.sql`
6. ✅ Implement commit pattern learning
   - Parse `git log` output
   - Extract type/scope from messages
   - Store in database
7. ✅ Update `git-commit` command
   - Analyze diff before commit
   - Generate message suggestions
   - Present to user for approval
   - Run quality gates if enabled
8. ✅ Add `git-quality-check` command
9. ✅ Add `git-hooks-install` command
10. ✅ Write unit tests for analyzer
11. ✅ Document analyzer API

**Deliverables**:
- Intelligent commit message generation working
- Basic quality gates (lint, format, secrets) functional
- Pre-commit hook installable
- Documentation updated

### Phase 2: Workflow Intelligence (Weeks 4-5)

**Goal**: Detect workflows and provide smart branching suggestions

**Tasks**:
1. ✅ Create `modules/git/include/git/workflow.hpp`
2. ✅ Implement `git::Workflow` class
   - Branch pattern analysis
   - Workflow classification algorithm
   - Branch name sanitization
   - Stale branch detection
3. ✅ Add database migration `006_git_workflows.sql`
4. ✅ Implement workflow detection
   - Analyze branch history
   - Calculate confidence scores
   - Store detected patterns
5. ✅ Add `git-workflow` command to show detected workflow
6. ✅ Add `git-branch` command with smart suggestions
   - Suggest name from task description
   - Validate against workflow conventions
   - Create and checkout branch
7. ✅ Update `git-status` to show workflow context
8. ✅ Implement stale branch cleanup command
9. ✅ Write workflow detection tests
10. ✅ Update documentation

**Deliverables**:
- Workflow detection functional (gitflow, trunk-based, GitHub flow)
- Smart branch naming suggestions
- Workflow-aware status display
- Stale branch detection

### Phase 3: Platform Integration (Weeks 6-8)

**Goal**: Connect to GitHub/GitLab for PR/MR workflows

**Tasks**:
1. ✅ Create `modules/git/include/git/platform.hpp` (base interface)
2. ✅ Create `modules/git/include/git/github.hpp`
3. ✅ Create `modules/git/include/git/gitlab.hpp`
4. ✅ Add libcurl dependency to CMakeLists.txt
5. ✅ Implement GitHub platform client
   - API authentication (token-based)
   - PR creation
   - PR status checking
   - CI status queries
6. ✅ Implement GitLab platform client
   - MR creation
   - MR status
   - Pipeline status
7. ✅ Add database migration `007_git_platforms.sql`
8. ✅ Add `git-pr` command for creating PRs
9. ✅ Add `git-pr-status` command
10. ✅ Implement PR template generation
    - Parse commits since base
    - Format as changelog
    - Add issue references
11. ✅ Add credential storage (encrypted in nexus)
12. ✅ Write platform integration tests (mock API)
13. ✅ Document platform setup

**Deliverables**:
- GitHub PR creation functional
- GitLab MR creation functional
- PR templates auto-generated from commits
- Platform credentials stored securely

### Phase 4: Advanced Features (Weeks 9-10)

**Goal**: Add conflict resolution, history analysis, and advanced helpers

**Tasks**:
1. ✅ Implement conflict resolution assistant
   - Parse merge conflicts
   - Show context for both sides
   - Suggest resolution strategies
2. ✅ Implement history analysis
   - Analyze commit frequency per file
   - Identify code owners
   - Suggest reviewers
3. ✅ Add stash management helpers
4. ✅ Add rebase assistance
5. ✅ Implement coverage tracking in quality gates
6. ✅ Add git command suggestions in engine
7. ✅ Write comprehensive integration tests
8. ✅ Performance optimization
9. ✅ Final documentation pass

**Deliverables**:
- Conflict resolution helper
- Code owner detection
- Complete git intelligence suite
- Performance benchmarks

---

## 9. Testing Strategy

### 9.1 Unit Tests

**Analyzer Tests** (`tests/git_analyzer_test.cpp`):
```cpp
TEST(AnalyzerTest, DetectsFeatureCommit) {
  DiffAnalysis diff;
  diff.added_files = {"src/auth.cpp", "include/auth.hpp"};
  diff.modified_files = {"src/main.cpp"};

  Analyzer analyzer(nullptr, nullptr);
  auto suggestion = analyzer.suggest_commit_message(diff,
                                                     CommitConvention::CONVENTIONAL);

  EXPECT_EQ(suggestion.type, "feat");
  EXPECT_EQ(suggestion.scope, "auth");
  EXPECT_GT(suggestion.confidence, 0.7);
}

TEST(AnalyzerTest, DetectsFixCommit) {
  DiffAnalysis diff;
  diff.modified_files = {"src/auth.cpp"};
  diff.lines_removed_per_file["src/auth.cpp"] = 5;
  diff.lines_added_per_file["src/auth.cpp"] = 3;

  Analyzer analyzer(nullptr, nullptr);
  auto suggestion = analyzer.suggest_commit_message(diff,
                                                     CommitConvention::CONVENTIONAL);

  EXPECT_EQ(suggestion.type, "fix");
}
```

**Quality Tests** (`tests/git_quality_test.cpp`):
```cpp
TEST(QualityTest, DetectsSecretsInDiff) {
  // Create test file with AWS key
  std::ofstream test_file("secret.txt");
  test_file << "AWS_KEY=AKIAIOSFODNN7EXAMPLE\n";
  test_file.close();

  Quality quality(nullptr, nullptr);
  auto result = quality.scan_secrets(".");

  EXPECT_FALSE(result.passed);
  EXPECT_EQ(result.errors.size(), 1);
  EXPECT_THAT(result.errors[0], HasSubstr("AWS"));
}
```

**Workflow Tests** (`tests/git_workflow_test.cpp`):
```cpp
TEST(WorkflowTest, DetectsGitflow) {
  // Mock branches
  std::vector<std::string> branches = {
    "main", "develop",
    "feature/user-auth", "feature/api-v2",
    "hotfix/critical-bug"
  };

  Workflow workflow(nullptr, nullptr);
  auto type = workflow.classify_workflow(branches);

  EXPECT_EQ(type, WorkflowType::GITFLOW);
}
```

### 9.2 Integration Tests

**End-to-End Commit Flow** (`tests/integration/git_commit_test.cpp`):
```cpp
TEST(GitIntegrationTest, IntelligentCommitFlow) {
  // Setup test repo
  TestRepo repo("test_commit_flow");
  repo.init();

  // Make changes
  repo.add_file("src/auth.cpp", "// New authentication code");
  repo.add_file("tests/auth_test.cpp", "// Tests");

  // Run commit with intelligence
  git::Client client(repo.path(), &log);
  git::Analyzer analyzer(&store, &log);
  git::Quality quality(&store, &log);

  // Analyze diff
  auto diff = analyzer.analyze_diff(repo.path());
  EXPECT_EQ(diff.added_files.size(), 2);

  // Generate message
  auto suggestion = analyzer.suggest_commit_message(diff,
                                                     CommitConvention::CONVENTIONAL);
  EXPECT_EQ(suggestion.type, "feat");

  // Run quality gates
  QualityConfig config;
  config.enabled_gates = {QualityGate::SECRET_SCAN};
  auto results = quality.run_gates(repo.path(), config);
  EXPECT_TRUE(results[0].passed);

  // Commit
  std::string message = suggestion.type + ": " + suggestion.subject;
  client.add_all();
  EXPECT_TRUE(client.commit(message));
}
```

### 9.3 Mock Testing for Platforms

```cpp
class MockGitHubAPI {
public:
  MOCK_METHOD(std::string, create_pr, (const PRTemplate&));
  MOCK_METHOD(PRStatus, get_pr, (int pr_number));
};

TEST(PlatformTest, CreatesPRWithTemplate) {
  MockGitHubAPI mock_api;

  EXPECT_CALL(mock_api, create_pr(_))
    .WillOnce(Return(R"({"number": 42, "url": "https://github.com/..."})"));

  GitHubPlatform platform(&mock_api, &log);
  PRTemplate tmpl;
  tmpl.title = "feat: Add authentication";

  auto status = platform.create_pr(".", tmpl);
  EXPECT_EQ(status.number, 42);
}
```

---

## 10. Security Considerations

### 10.1 Credential Storage

**Problem**: Need to store GitHub/GitLab API tokens securely

**Solution**:
```cpp
// Encrypt tokens before storing in nexus
std::string encrypt_token(const std::string &token) {
  // Use system keyring if available (libsecret on Linux)
  // Fall back to AES-256 encryption with key derived from user password
  // Never store tokens in plaintext
}

// In git::Platform initialization
std::string api_token = decrypt_token(stored_token);
```

**Alternatives**:
1. Use system keyring (Gnome Keyring, KWallet)
2. Prompt for token each session
3. Use OAuth flow instead of personal tokens

### 10.2 Secret Scanning

**Patterns to detect**:
```cpp
const std::vector<std::regex> SECRET_PATTERNS = {
  // AWS
  std::regex(R"(AKIA[0-9A-Z]{16})"),
  std::regex(R"(aws_secret_access_key\s*=\s*[A-Za-z0-9/+=]{40})"),

  // GitHub
  std::regex(R"(ghp_[a-zA-Z0-9]{36})"),
  std::regex(R"(github_pat_[a-zA-Z0-9]{22}_[a-zA-Z0-9]{59})"),

  // Private keys
  std::regex(R"(-----BEGIN (RSA|OPENSSH|DSA|EC) PRIVATE KEY-----)"),

  // Generic passwords (high false positive rate)
  std::regex(R"((password|passwd|pwd)\s*[:=]\s*['"][^'"]{8,}['"])"),

  // API keys
  std::regex(R"(api[_-]?key\s*[:=]\s*['"][A-Za-z0-9]{20,}['"])"),
};
```

**Whitelisting**: Allow `.env.example`, `config.example.yml` files

### 10.3 Command Injection Prevention

**Always use shell_quote**:
```cpp
// WRONG - vulnerable to injection
std::string cmd = "git commit -m " + message;

// CORRECT
std::string cmd = "git commit -m " + nazg::system::shell_quote(message);
```

**Validate inputs**:
```cpp
bool is_valid_branch_name(const std::string &name) {
  // Reject dangerous characters
  return name.find_first_of(";&|><`$(){}[]!") == std::string::npos;
}
```

### 10.4 API Rate Limiting

**GitHub**: 5000 requests/hour (authenticated)
**GitLab**: 2000 requests/hour (self-hosted may vary)

**Mitigation**:
```cpp
class RateLimiter {
  std::chrono::steady_clock::time_point last_request_;
  int requests_this_hour_ = 0;

public:
  bool can_make_request() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_request_ > 1h) {
      requests_this_hour_ = 0;
    }
    return requests_this_hour_ < 4500;  // Leave margin
  }
};
```

---

## 11. Performance Considerations

### 11.1 Git Operations

**Problem**: `git log`, `git diff`, `git status` can be slow on large repos

**Optimizations**:
1. Use `--porcelain=v2` for machine-readable output (already done)
2. Limit `git log` queries: `git log -n 100` instead of full history
3. Use `--numstat` for faster diff statistics
4. Cache workflow detection results in nexus

```cpp
// Cache workflow detection for 24 hours
auto cached = store_->get_workflow(project_id);
if (cached && (now - cached->detected_at) < 86400) {
  return *cached;
}

// Re-detect and update cache
auto pattern = detect_workflow(repo_path);
store_->set_workflow(project_id, pattern);
```

### 11.2 Pattern Learning

**Problem**: Analyzing 100+ commits for pattern learning is slow

**Optimization**:
```cpp
// Learn incrementally - only new commits since last learning
int64_t last_learned_timestamp = store_->get_last_learned_timestamp(project_id);
std::string cmd = "git log --since=" + std::to_string(last_learned_timestamp) +
                  " --format=%H|%s";

// Batch insert patterns
store_->begin_transaction();
for (const auto &commit : commits) {
  store_->add_commit_pattern(project_id, commit);
}
store_->commit_transaction();
```

### 11.3 API Requests

**Problem**: GitHub/GitLab API calls add latency

**Mitigation**:
1. Cache PR status for 5 minutes
2. Batch API requests when possible
3. Use conditional requests (ETag) to avoid re-fetching
4. Make API calls async/background when not critical

---

## 12. CLI Command Reference

### New Commands

| Command | Description | Example |
|---------|-------------|---------|
| `nazg git-commit` | Smart commit with AI message | `nazg git-commit` |
| `nazg git-quality-check` | Run quality gates manually | `nazg git-quality-check` |
| `nazg git-hooks-install` | Install pre-commit hooks | `nazg git-hooks-install` |
| `nazg git-workflow` | Show detected workflow | `nazg git-workflow` |
| `nazg git-branch <task>` | Create branch with smart name | `nazg git-branch "add user auth"` |
| `nazg git-pr [title]` | Create PR/MR | `nazg git-pr "Add authentication"` |
| `nazg git-pr-status` | Check PR status and CI | `nazg git-pr-status` |
| `nazg git-stale` | List stale branches | `nazg git-stale --days 30` |
| `nazg git-analyze` | Analyze commit history | `nazg git-analyze --commits 100` |

### Enhanced Commands

| Command | Enhancement |
|---------|-------------|
| `nazg git-status` | Now shows workflow type, suggests next action |
| `nazg update` | Considers git status in decision making |
| `nazg status` | Includes PR status if available |

---

## 13. Configuration

### Per-Project Config

Store in `nazg.toml` or `.nazg/config.toml`:

```toml
[git]
# Commit message convention
convention = "conventional"  # or "semantic", "simple", "custom"

# Quality gates
quality_gates = ["lint", "format", "test", "secrets"]
auto_fix_format = true
block_on_failure = true

# Workflow (auto-detected but can override)
workflow = "gitflow"  # or "github_flow", "trunk_based"

# Platform integration
platform = "github"  # or "gitlab", "gitea"
platform_token_env = "GITHUB_TOKEN"  # Read from environment

# Branch naming
branch_prefix_feature = "feature/"
branch_prefix_hotfix = "hotfix/"
branch_prefix_release = "release/"

# Stale branch threshold
stale_days = 30
```

### Global Config

Store in `~/.config/nazg/git.toml`:

```toml
[platforms.github]
api_endpoint = "https://api.github.com"
# Token stored in system keyring, not in file

[platforms.gitlab]
api_endpoint = "https://gitlab.com/api/v4"

[quality]
# Global quality preferences
enable_secret_scan = true
secret_patterns_file = "~/.config/nazg/secret_patterns.txt"
```

---

## 14. Rollout Plan

### Week 1-3: Phase 1 Development
- Implement Analyzer and Quality classes
- Add database migrations
- Update git-commit command
- Internal testing

### Week 4-5: Phase 2 Development
- Implement Workflow class
- Add workflow detection
- Test on various project types

### Week 6-8: Phase 3 Development
- Platform integration (GitHub first)
- PR creation workflow
- Credential management

### Week 9-10: Phase 4 & Polish
- Advanced features
- Documentation
- Performance tuning
- User testing

### Week 11: Beta Release
- Release to select users
- Gather feedback
- Fix critical issues

### Week 12: General Release
- Announce feature
- Update documentation
- Monitor usage and issues

---

## 15. Success Metrics

### Adoption Metrics
- % of commits using AI-generated messages
- % of projects with quality gates enabled
- Number of PRs created via nazg

### Quality Metrics
- Reduction in commit message length variance
- Reduction in commits failing CI
- Reduction in credential leaks detected

### User Satisfaction
- User feedback on message quality (1-5 scale)
- Time saved vs manual commit workflow
- Feature request analysis

---

## 16. Future Enhancements (Post-v1)

### Advanced AI Features
1. **Commit message from voice** - Speak commit description, generate message
2. **Auto-squash suggestions** - Detect WIP commits and suggest squashing
3. **Semantic versioning** - Auto-bump version based on commit types
4. **Changelog generation** - Auto-generate CHANGELOG.md from commits

### Advanced Workflow
5. **Interactive rebase helper** - Guide through complex rebases
6. **Merge conflict resolution UI** - TUI for resolving conflicts
7. **Code review assistant** - Suggest reviewers, generate review checklists
8. **Release automation** - Create releases with auto-generated notes

### Platform Features
9. **Bitbucket integration** - Support for Bitbucket Cloud/Server
10. **Gitea integration** - Self-hosted git platform
11. **CI/CD integration** - Trigger builds, check status
12. **Issue tracker sync** - Bi-directional sync with issue trackers

---

## Appendix A: Example Workflows

### Example 1: Feature Development (Gitflow)

```bash
# Start new feature
$ nazg git-branch "add user authentication"
Detected workflow: Gitflow
Suggested branch: feature/add-user-authentication
Create from: develop
[✓] Created and switched to feature/add-user-authentication

# Make changes...
$ vim src/auth.cpp

# Commit with intelligence
$ nazg git-commit
Analyzing changes...
─────────────────────────────
Added: src/auth.cpp, include/auth.hpp, tests/auth_test.cpp
Modified: src/main.cpp

Suggested commit message:
  feat(auth): add user authentication module

Confidence: 87%

Running quality gates...
  ✓ Linter (clang-tidy): passed
  ✓ Formatter (clang-format): passed
  ✓ Secrets scan: passed
  ✓ Tests: 15 passed, 0 failed

Commit with this message? [Y/n] y
[feature/add-user-authentication abc123f] feat(auth): add user authentication module

# Create PR
$ nazg git-pr
Detected workflow: Gitflow (merge to develop)
Analyzing commits since develop...

PR Template:
─────────────────────────────
Title: feat(auth): add user authentication module
Base: develop

Description:
## Changes
- Add user authentication module
- Implement login/logout endpoints
- Add authentication tests

## Test Plan
- [x] Unit tests pass (15/15)
- [x] Linter passes
- [ ] Integration tests (manual)

Create PR? [Y/n] y
✓ PR created: https://github.com/owner/repo/pull/42
```

### Example 2: Hotfix (Trunk-based)

```bash
# Detected critical bug in production
$ nazg git-branch "fix login crash"
Detected workflow: Trunk-based
Suggested branch: fix-login-crash
Create from: main
[✓] Created and switched to fix-login-crash

# Fix the bug
$ vim src/auth.cpp

# Commit
$ nazg git-commit
Analyzing changes...
Modified: src/auth.cpp (12 insertions, 3 deletions)

Suggested commit message:
  fix(auth): prevent null pointer crash in login handler

Type confidence: 94% (fix)
Detected pattern: Most commits in auth.cpp are type 'fix'

Quality gates:
  ✓ Tests: 15 passed, 0 failed
  ✓ No secrets detected

Commit? [Y/n] y
[fix-login-crash def456a] fix(auth): prevent null pointer crash in login handler

# Immediate PR for hotfix
$ nazg git-pr --urgent
Creating urgent PR (no draft)...
Reviewers suggested based on file history:
  - alice (80% of auth.cpp commits)
  - bob (owner of auth module)

PR created: https://github.com/owner/repo/pull/43
✓ Requested reviews from alice, bob
✓ Added label: urgent
```

---

## Appendix B: Commit Type Reference

### Conventional Commits Types

| Type | Description | Example |
|------|-------------|---------|
| `feat` | New feature | `feat(api): add user registration endpoint` |
| `fix` | Bug fix | `fix(auth): handle expired token correctly` |
| `docs` | Documentation only | `docs(readme): update installation instructions` |
| `style` | Code style/formatting (no logic change) | `style(core): format with clang-format` |
| `refactor` | Code restructuring (no feature/fix) | `refactor(db): extract query builder` |
| `perf` | Performance improvement | `perf(search): optimize index lookup` |
| `test` | Add or update tests | `test(auth): add token expiration tests` |
| `chore` | Maintenance tasks | `chore(deps): update dependencies` |
| `ci` | CI/CD changes | `ci(github): add coverage reporting` |
| `build` | Build system changes | `build(cmake): add sanitizer option` |
| `revert` | Revert previous commit | `revert: "feat(api): add user registration"` |

### Scope Examples

| Scope | Description |
|-------|-------------|
| `api` | API-related changes |
| `auth` | Authentication/authorization |
| `db` | Database schema or queries |
| `ui` | User interface |
| `core` | Core functionality |
| `*` | Multiple modules affected |

---

## Appendix C: Quality Gate Configuration Examples

### Minimal (Fast Commits)
```toml
[git.quality]
gates = ["secrets"]
block_on_failure = true
```

### Balanced (Recommended)
```toml
[git.quality]
gates = ["lint", "format", "secrets"]
auto_fix_format = true
block_on_failure = true
```

### Strict (High Quality)
```toml
[git.quality]
gates = ["lint", "format", "test", "secrets", "coverage"]
auto_fix_format = true
block_on_failure = true
coverage_threshold = 80
test_timeout = 300
```

### Custom Linter Config
```toml
[git.quality.lint]
cpp = "clang-tidy --config-file=.clang-tidy"
python = "ruff check --fix"
rust = "cargo clippy -- -D warnings"

[git.quality.format]
cpp = "clang-format -i"
python = "black"
rust = "cargo fmt"
```

---

## Conclusion

This enhancement plan transforms Nazg's git module from a basic wrapper into an intelligent assistant that:

1. **Saves time** - Auto-generates quality commit messages
2. **Prevents errors** - Quality gates catch issues before commit
3. **Enforces standards** - Learns and follows project conventions
4. **Streamlines workflow** - Detects and adapts to team practices
5. **Integrates platforms** - Seamless PR/MR creation

Implementation will proceed in 4 phases over 10 weeks, with incremental delivery of value at each phase.

**Next Steps**:
1. Review and approve this plan
2. Begin Phase 1 implementation (Analyzer + Quality)
3. Set up testing infrastructure
4. Create tracking issues for each phase

---

**Document Status**: Draft v1.0 - Pending Review
**Last Updated**: 2025-10-06
**Author**: Nazg Project

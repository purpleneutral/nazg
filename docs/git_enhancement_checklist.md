# Git Module Enhancement Checklist

**Plan Document**: [git_enhancement_plan.md](git_enhancement_plan.md)
**Status**: Planning Phase
**Last Updated**: 2025-10-06

---

## Quick Command Reference

```bash
# After implementation
nazg git-commit              # Smart commit with AI-generated message
nazg git-quality-check       # Run quality gates manually
nazg git-hooks-install       # Install pre-commit hooks
nazg git-workflow            # Show detected workflow
nazg git-branch <desc>       # Create branch with smart name
nazg git-pr [title]          # Create PR/MR on GitHub/GitLab
nazg git-pr-status           # Check PR and CI status
nazg git-stale               # List stale branches
```

---

## Phase 1: Intelligence Foundation (Weeks 1-3)

**Goal**: AI-powered commit messages and basic quality gates

### 1.1 Commit Message Intelligence

- [ ] Create `modules/git/include/git/analyzer.hpp`
  - [ ] Define `DiffAnalysis` struct
  - [ ] Define `CommitSuggestion` struct
  - [ ] Define `CommitConvention` enum
  - [ ] Declare `Analyzer` class interface
- [ ] Implement `modules/git/src/analyzer.cpp`
  - [ ] `analyze_diff()` - Parse git diff output
  - [ ] `suggest_commit_message()` - Generate message from analysis
  - [ ] `learn_from_history()` - Extract patterns from git log
  - [ ] `detect_convention()` - Identify project's commit style
  - [ ] `validate_message()` - Check message format
  - [ ] `infer_type()` - Determine commit type (feat/fix/docs/etc)
  - [ ] `infer_scope()` - Detect affected module/component
  - [ ] `generate_subject()` - Create commit subject line
- [ ] Add database migration `migrations/005_git_patterns.sql`
  - [ ] Create `git_commit_patterns` table
  - [ ] Add indexes for performance
- [ ] Update `nexus::Store` with pattern storage methods
  - [ ] `add_commit_pattern()`
  - [ ] `get_commit_patterns()`
  - [ ] `get_most_common_type()`
  - [ ] `get_most_common_scope()`

### 1.2 Quality Gates

- [ ] Create `modules/git/include/git/quality.hpp`
  - [ ] Define `QualityGate` enum
  - [ ] Define `QualityResult` struct
  - [ ] Define `QualityConfig` struct
  - [ ] Declare `Quality` class interface
- [ ] Implement `modules/git/src/quality.cpp`
  - [ ] `run_gates()` - Execute all configured gates
  - [ ] `run_gate()` - Execute specific gate
  - [ ] `run_linter()` - Language-specific linting
  - [ ] `run_formatter()` - Format checking
  - [ ] `run_tests()` - Integration with test module
  - [ ] `scan_secrets()` - Regex-based secret detection
  - [ ] `check_message_format()` - Validate commit message
  - [ ] `auto_fix()` - Auto-fix formatting issues
  - [ ] `recommend_config()` - Suggest gates for project
  - [ ] `install_hook()` - Install pre-commit hook

### 1.3 Command Integration

- [ ] Update `modules/git/src/commands.cpp`
  - [ ] Enhance `cmd_git_commit()` with analyzer integration
    - [ ] Analyze staged diff
    - [ ] Generate message suggestions
    - [ ] Present to user with confidence score
    - [ ] Run quality gates if enabled
    - [ ] Store commit pattern in nexus
  - [ ] Add `cmd_git_quality_check()` - Manual quality gate execution
  - [ ] Add `cmd_git_hooks_install()` - Install pre-commit hooks
  - [ ] Add `cmd_git_hooks_uninstall()` - Remove hooks
- [ ] Register new commands in `register_commands()`

### 1.4 Testing

- [ ] Create `tests/git_analyzer_test.cpp`
  - [ ] Test type inference (feat, fix, docs, test, etc.)
  - [ ] Test scope detection
  - [ ] Test message generation
  - [ ] Test convention detection
  - [ ] Test validation
- [ ] Create `tests/git_quality_test.cpp`
  - [ ] Test secret scanning with known patterns
  - [ ] Test linter integration
  - [ ] Test formatter integration
  - [ ] Test gate result aggregation
- [ ] Integration test: full commit flow with intelligence

### 1.5 Documentation

- [ ] Update `docs/git.md` with analyzer API
- [ ] Add quality gates documentation
- [ ] Add commit convention reference
- [ ] Add examples of generated messages
- [ ] Update README with new features

**Phase 1 Status**: ⬜ Not Started

---

## Phase 2: Workflow Intelligence (Weeks 4-5)

**Goal**: Detect git workflows and provide smart branching

### 2.1 Workflow Detection

- [ ] Create `modules/git/include/git/workflow.hpp`
  - [ ] Define `WorkflowType` enum
  - [ ] Define `WorkflowPattern` struct
  - [ ] Define `BranchSuggestion` struct
  - [ ] Declare `Workflow` class interface
- [ ] Implement `modules/git/src/workflow.cpp`
  - [ ] `detect()` - Analyze branches and detect workflow
  - [ ] `suggest_branch()` - Generate branch name from description
  - [ ] `validate_branch_name()` - Check against conventions
  - [ ] `suggest_next_action()` - Context-aware suggestions
  - [ ] `detect_stale_branches()` - Find old branches
  - [ ] `classify_workflow()` - Determine workflow type
  - [ ] `sanitize_branch_name()` - Clean user input for branch names
- [ ] Add database migration `migrations/006_git_workflows.sql`
  - [ ] Create `git_workflows` table
  - [ ] Create `git_branches` table (lifecycle tracking)
  - [ ] Add indexes
- [ ] Update `nexus::Store` with workflow methods
  - [ ] `set_workflow()`
  - [ ] `get_workflow()`
  - [ ] `add_branch()`
  - [ ] `mark_branch_merged()`

### 2.2 Smart Branching

- [ ] Update `modules/git/src/commands.cpp`
  - [ ] Add `cmd_git_workflow()` - Display detected workflow
  - [ ] Add `cmd_git_branch()` - Create branch with smart naming
    - [ ] Detect current workflow
    - [ ] Sanitize task description
    - [ ] Suggest branch name
    - [ ] Validate against conventions
    - [ ] Create and checkout branch
  - [ ] Add `cmd_git_stale()` - List and clean stale branches
  - [ ] Enhance `cmd_git_status()` - Show workflow context
- [ ] Register new commands

### 2.3 Testing

- [ ] Create `tests/git_workflow_test.cpp`
  - [ ] Test gitflow detection
  - [ ] Test GitHub flow detection
  - [ ] Test trunk-based detection
  - [ ] Test branch name sanitization
  - [ ] Test stale branch detection
- [ ] Integration test: workflow detection on real repos

### 2.4 Documentation

- [ ] Update `docs/git.md` with workflow API
- [ ] Add workflow detection documentation
- [ ] Add branching strategy guide
- [ ] Add examples for each workflow type

**Phase 2 Status**: ⬜ Not Started

---

## Phase 3: Platform Integration (Weeks 6-8)

**Goal**: GitHub/GitLab PR/MR creation and CI integration

### 3.1 Platform Abstraction

- [ ] Create `modules/git/include/git/platform.hpp`
  - [ ] Define `PlatformType` enum
  - [ ] Define `PRTemplate` struct
  - [ ] Define `PRStatus` struct
  - [ ] Declare `Platform` interface (pure virtual)
  - [ ] Declare factory function `create_platform()`
- [ ] Implement `modules/git/src/platform.cpp`
  - [ ] Factory implementation
  - [ ] Platform type detection from remote URL
  - [ ] Common utilities (URL parsing, etc.)

### 3.2 GitHub Integration

- [ ] Add libcurl dependency to `CMakeLists.txt`
- [ ] Create `modules/git/include/git/github.hpp`
  - [ ] Declare `GitHubPlatform` class
- [ ] Implement `modules/git/src/github.cpp`
  - [ ] API authentication (token-based)
  - [ ] `create_pr()` - Create pull request
  - [ ] `get_pr_status()` - Get PR state and checks
  - [ ] `link_to_issue()` - Link commits to issues
  - [ ] `check_ci()` - Query GitHub Actions status
  - [ ] `generate_pr_template()` - Generate from commits
  - [ ] HTTP helpers (GET, POST with libcurl)
  - [ ] Parse owner/repo from remote URL

### 3.3 GitLab Integration

- [ ] Create `modules/git/include/git/gitlab.hpp`
  - [ ] Declare `GitLabPlatform` class
- [ ] Implement `modules/git/src/gitlab.cpp`
  - [ ] API authentication
  - [ ] `create_mr()` - Create merge request
  - [ ] `get_mr_status()` - Get MR state
  - [ ] `check_pipeline()` - Query pipeline status
  - [ ] `generate_mr_template()` - Generate from commits
  - [ ] HTTP helpers
  - [ ] Parse namespace/project from remote URL

### 3.4 Database & Credentials

- [ ] Add database migration `migrations/007_git_platforms.sql`
  - [ ] Create `git_platforms` table
  - [ ] Create `git_pull_requests` table
  - [ ] Add indexes
- [ ] Implement credential storage
  - [ ] Token encryption (AES-256 or keyring)
  - [ ] Token retrieval from environment variables
  - [ ] Secure storage in nexus
- [ ] Update `nexus::Store` with platform methods
  - [ ] `add_platform()`
  - [ ] `get_platform()`
  - [ ] `store_token()`
  - [ ] `retrieve_token()`
  - [ ] `add_pull_request()`
  - [ ] `get_pull_request_by_branch()`

### 3.5 Command Integration

- [ ] Update `modules/git/src/commands.cpp`
  - [ ] Add `cmd_git_pr()` - Create PR/MR
    - [ ] Detect platform from remote
    - [ ] Generate PR template from commits
    - [ ] Prompt for title/description
    - [ ] Create PR via API
    - [ ] Display URL
  - [ ] Add `cmd_git_pr_status()` - Check PR and CI status
  - [ ] Add `cmd_git_platform_setup()` - Configure API credentials
- [ ] Register new commands

### 3.6 Testing

- [ ] Create mock HTTP server for testing
- [ ] Create `tests/git_github_test.cpp`
  - [ ] Test PR creation with mock API
  - [ ] Test PR status parsing
  - [ ] Test CI check parsing
- [ ] Create `tests/git_gitlab_test.cpp`
  - [ ] Test MR creation with mock API
  - [ ] Test pipeline status parsing
- [ ] Integration test: PR creation flow (requires test account)

### 3.7 Documentation

- [ ] Update `docs/git.md` with platform integration
- [ ] Add GitHub setup guide
- [ ] Add GitLab setup guide
- [ ] Add credential management documentation
- [ ] Add PR workflow examples

**Phase 3 Status**: ⬜ Not Started

---

## Phase 4: Advanced Features (Weeks 9-10)

**Goal**: Conflict resolution, history analysis, advanced helpers

### 4.1 Conflict Resolution

- [ ] Create `modules/git/include/git/conflict.hpp`
  - [ ] Define `ConflictInfo` struct
  - [ ] Define `ResolutionStrategy` enum
  - [ ] Declare `ConflictResolver` class
- [ ] Implement `modules/git/src/conflict.cpp`
  - [ ] `detect_conflicts()` - Parse merge conflict markers
  - [ ] `get_conflict_context()` - Show both sides with history
  - [ ] `suggest_resolution()` - Recommend strategy
  - [ ] `apply_resolution()` - Execute resolution
- [ ] Add `cmd_git_conflicts()` command
  - [ ] List conflicts
  - [ ] Show context for each
  - [ ] Offer resolution options

### 4.2 History Analysis

- [ ] Create `modules/git/include/git/history.hpp`
  - [ ] Define `FileOwnership` struct
  - [ ] Define `CommitFrequency` struct
  - [ ] Declare `HistoryAnalyzer` class
- [ ] Implement `modules/git/src/history.cpp`
  - [ ] `analyze_file_ownership()` - Determine code owners
  - [ ] `suggest_reviewers()` - Based on file changes
  - [ ] `analyze_commit_frequency()` - Activity patterns
  - [ ] `detect_hotspots()` - Files changed frequently
- [ ] Add `cmd_git_analyze()` command
  - [ ] Show file ownership
  - [ ] Suggest reviewers for current changes

### 4.3 Stash Management

- [ ] Add stash helpers to `git::Client`
  - [ ] `stash_save()`
  - [ ] `stash_list()`
  - [ ] `stash_pop()`
  - [ ] `stash_apply()`
- [ ] Add `cmd_git_stash()` command
  - [ ] Interactive stash management
  - [ ] Named stashes with descriptions

### 4.4 Integration with Engine

- [ ] Update `modules/brain/src/planner.cpp`
  - [ ] Consider git status in `decide()`
  - [ ] Suggest commit action when staged changes exist
  - [ ] Suggest PR creation after successful tests
- [ ] Update `modules/engine/src/runtime.cpp`
  - [ ] Add git suggestions to update loop
  - [ ] Show PR status in `nazg status` output

### 4.5 Performance Optimization

- [ ] Implement caching for workflow detection
- [ ] Batch database inserts for commit patterns
- [ ] Optimize diff parsing (avoid multiple git calls)
- [ ] Add rate limiting for platform API calls
- [ ] Profile and optimize hot paths

### 4.6 Testing

- [ ] Create `tests/git_conflict_test.cpp`
- [ ] Create `tests/git_history_test.cpp`
- [ ] End-to-end integration tests
- [ ] Performance benchmarks
- [ ] Memory leak testing (valgrind)

### 4.7 Documentation

- [ ] Complete API documentation
- [ ] Add conflict resolution guide
- [ ] Add code ownership documentation
- [ ] Add performance tuning guide
- [ ] Update README with all features

**Phase 4 Status**: ⬜ Not Started

---

## Configuration Files

### Per-Project Configuration

Create `nazg.toml` in project root:

```toml
[git]
convention = "conventional"
quality_gates = ["lint", "format", "test", "secrets"]
auto_fix_format = true
block_on_failure = true
workflow = "gitflow"  # or auto-detect
platform = "github"
platform_token_env = "GITHUB_TOKEN"
stale_days = 30

[git.branch_naming]
feature = "feature/"
hotfix = "hotfix/"
release = "release/"
```

### Global Configuration

Create `~/.config/nazg/git.toml`:

```toml
[platforms.github]
api_endpoint = "https://api.github.com"

[platforms.gitlab]
api_endpoint = "https://gitlab.com/api/v4"

[quality]
enable_secret_scan = true
secret_patterns_file = "~/.config/nazg/secret_patterns.txt"
```

---

## Dependencies

### Required
- **libcurl** (or libcurl-dev) - For HTTP API calls to GitHub/GitLab
- **OpenSSL** (or libssl-dev) - For token encryption (if not using system keyring)

### Optional
- **libsecret** (Linux) - System keyring integration for secure token storage
- **libgit2** - Alternative to shell git commands (performance improvement)

### Installation (Ubuntu/Debian)
```bash
sudo apt install libcurl4-openssl-dev libssl-dev
sudo apt install libsecret-1-dev  # Optional, for keyring
```

### Installation (Arch)
```bash
sudo pacman -S curl openssl
sudo pacman -S libsecret  # Optional
```

---

## Testing Strategy

### Unit Tests
- Test each analyzer method independently
- Test quality gates with mocked git repos
- Test workflow detection with synthetic branch lists
- Test platform API with mocked HTTP responses

### Integration Tests
- Full commit flow: analyze → suggest → quality gates → commit
- Full PR flow: commits → template → API call → status
- Workflow detection on real open-source repos

### Manual Testing
- Test on projects with different languages
- Test on projects with different workflows
- Test with real GitHub/GitLab accounts
- Test secret detection with known patterns

---

## Known Limitations

1. **Secret Scanning**: Regex-based, may have false positives/negatives
2. **Commit Message Generation**: AI confidence varies, may need user editing
3. **Workflow Detection**: Heuristic-based, may misclassify edge cases
4. **Platform APIs**: Rate limited (GitHub: 5000/hr, GitLab: 2000/hr)
5. **Performance**: Large repos (>100k commits) may be slow for history analysis

---

## Migration Path

### For Existing Nazg Users

1. **Update binary**: `nazg update` or rebuild from source
2. **Run migrations**: Automatic on first run (005-007)
3. **Install hooks** (optional): `nazg git-hooks-install`
4. **Configure platform** (optional): `nazg git-platform-setup`
5. **Test commit flow**: `nazg git-commit`

### Backward Compatibility

- All existing git commands continue to work unchanged
- New features are opt-in (quality gates disabled by default initially)
- Can disable AI features via config if desired

---

## Rollout Timeline

| Week | Phase | Deliverable | Status |
|------|-------|-------------|--------|
| 1-3  | Phase 1 | Analyzer + Quality Gates | ⬜ |
| 4-5  | Phase 2 | Workflow Detection | ⬜ |
| 6-8  | Phase 3 | Platform Integration | ⬜ |
| 9-10 | Phase 4 | Advanced Features | ⬜ |
| 11   | Beta    | User Testing | ⬜ |
| 12   | Release | General Availability | ⬜ |

---

## Success Criteria

### Phase 1 Complete When:
- ✅ Commit message generation works with >70% user acceptance
- ✅ Quality gates can be run manually and via hooks
- ✅ Secret scanning detects common patterns
- ✅ Unit tests pass with >80% coverage

### Phase 2 Complete When:
- ✅ Workflow detection accurate for gitflow/trunk-based/GitHub flow
- ✅ Branch naming suggestions work for common descriptions
- ✅ Stale branch detection finds branches >30 days old

### Phase 3 Complete When:
- ✅ Can create PRs on GitHub and GitLab
- ✅ PR templates generated from commit history
- ✅ CI status checks work
- ✅ Credentials stored securely

### Phase 4 Complete When:
- ✅ All features integrated and tested
- ✅ Performance acceptable on large repos
- ✅ Documentation complete
- ✅ Ready for beta release

---

**Overall Status**: Planning Phase (0% Complete)
**Next Action**: Begin Phase 1 implementation (analyzer.hpp creation)
**Last Updated**: 2025-10-06

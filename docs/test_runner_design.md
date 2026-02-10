# Test Runner Integration - Design Document

**Status:** Planning
**Created:** 2025-10-06
**Target Module:** `modules/test` (new)
**Dependencies:** brain, task, nexus, directive, system

---

## 1. Overview

Extend Nazg's intelligence to automatically detect, run, and track tests across multiple frameworks and languages. The test runner will integrate seamlessly with the existing Brain → Plan → Execute workflow, providing developers with test results, coverage tracking, and failure diagnostics.

### Goals

- **Automatic detection** – Identify test frameworks from project structure (gtest, pytest, cargo test, jest, etc.)
- **Unified execution** – Run tests through a common interface regardless of underlying framework
- **History tracking** – Store test results, durations, and coverage in Nexus over time
- **Smart recommendations** – Brain suggests when to run tests (after build, on file changes, before commit)
- **Failure diagnostics** – Parse test output, extract failures, suggest fixes
- **Assistant integration** – Surface test status and offer actions in interactive mode

### Non-Goals (v1)

- Custom test framework implementation
- Code coverage instrumentation (rely on framework tools)
- Distributed test execution across multiple machines
- Test case generation or mutation testing

---

## 2. Architecture

### 2.1 Component Diagram

```
┌──────────────────────────────────────────────────────────┐
│                     brain::Detector                       │
│  (Enhanced to detect test frameworks and test files)     │
└────────────────────┬─────────────────────────────────────┘
                     │ detects test_framework
                     ▼
┌──────────────────────────────────────────────────────────┐
│                    brain::Planner                         │
│    (Enhanced with Action::TEST, generates test plans)    │
└────────────────────┬─────────────────────────────────────┘
                     │ plan.action = TEST
                     ▼
┌──────────────────────────────────────────────────────────┐
│                    task::Builder                          │
│      (Dispatches to test::Runner for TEST actions)       │
└────────────────────┬─────────────────────────────────────┘
                     │
                     ▼
┌──────────────────────────────────────────────────────────┐
│                    test::Runner (NEW)                     │
│  - Framework detection and adapter selection             │
│  - Test execution and output capture                     │
│  - Result parsing and classification                     │
│  - Coverage aggregation                                  │
└────────────────────┬─────────────────────────────────────┘
                     │
                     ▼
┌──────────────────────────────────────────────────────────┐
│                   nexus::Store (ENHANCED)                 │
│  - test_runs table (framework, duration, pass/fail)      │
│  - test_results table (individual test cases)            │
│  - test_coverage table (file, line %, branch %)          │
└──────────────────────────────────────────────────────────┘
```

### 2.2 Module Structure

```
modules/test/
├── include/test/
│   ├── detector.hpp       # Test framework detection
│   ├── runner.hpp         # Main test execution engine
│   ├── adapters.hpp       # Framework-specific adapters
│   ├── parser.hpp         # Test output parsing
│   ├── types.hpp          # TestResult, TestRun, Coverage types
│   └── commands.hpp       # CLI command registration
├── src/
│   ├── detector.cpp
│   ├── runner.cpp
│   ├── adapters/
│   │   ├── gtest.cpp      # GoogleTest adapter
│   │   ├── pytest.cpp     # pytest adapter
│   │   ├── cargo.cpp      # cargo test adapter
│   │   ├── jest.cpp       # Jest/Vitest adapter
│   │   ├── ctest.cpp      # CTest adapter
│   │   └── catch2.cpp     # Catch2 adapter
│   ├── parser.cpp
│   └── commands.cpp
└── CMakeLists.txt
```

---

## 3. Core Types

### 3.1 Test Framework Enumeration

```cpp
// modules/test/include/test/types.hpp
namespace nazg::test {

enum class Framework {
  UNKNOWN,
  GTEST,        // Google Test (C++)
  CATCH2,       // Catch2 (C++)
  PYTEST,       // pytest (Python)
  UNITTEST,     // unittest (Python)
  CARGO,        // cargo test (Rust)
  JEST,         // Jest (JavaScript/TypeScript)
  VITEST,       // Vitest (JavaScript/TypeScript)
  GO_TEST,      // go test (Go)
  CTEST,        // CTest (CMake)
};

struct TestFrameworkInfo {
  Framework framework;
  std::string name;
  std::string version;              // e.g., "1.14.0"
  std::string command;              // Base command to run
  std::vector<std::string> args;    // Default arguments
  std::string test_dir;             // Where tests live
  std::optional<std::string> config_file;  // pytest.ini, jest.config.js, etc.
};

} // namespace nazg::test
```

### 3.2 Test Execution Results

```cpp
enum class TestStatus {
  PASSED,
  FAILED,
  SKIPPED,
  ERROR,        // Test crashed or couldn't run
  TIMEOUT,
};

struct TestCase {
  std::string name;           // "MyTest.BasicAssertion"
  std::string suite;          // "MyTest"
  TestStatus status;
  int64_t duration_ms;
  std::string message;        // Failure message or skip reason
  std::string file;           // Source file (if parseable)
  int line;                   // Line number (if parseable)
};

struct TestRun {
  int64_t id;                 // Nexus row ID
  int64_t project_id;
  Framework framework;
  int64_t timestamp;
  int64_t duration_ms;
  int total;
  int passed;
  int failed;
  int skipped;
  int errors;
  std::vector<TestCase> cases;
  std::optional<Coverage> coverage;
};

struct Coverage {
  float line_coverage;        // 0.0 - 1.0
  float branch_coverage;      // 0.0 - 1.0
  int lines_covered;
  int lines_total;
  int branches_covered;
  int branches_total;
  std::unordered_map<std::string, FileCoverage> files;
};

struct FileCoverage {
  std::string path;
  float line_coverage;
  std::vector<int> covered_lines;
  std::vector<int> uncovered_lines;
};
```

---

## 4. Detection Logic

### 4.1 Enhance `brain::Detector`

Add test framework detection to existing project detection:

```cpp
// modules/brain/include/brain/detector.hpp
struct ProjectInfo {
  // ... existing fields ...
  std::string test_framework;  // "gtest", "pytest", etc.
  bool has_tests;
  std::string test_directory;  // "tests/", "test/", "__tests__/", etc.
};

// modules/brain/src/detector.cpp
void Detector::detect_test_framework(ProjectInfo& info, const std::string& root) {
  // C++ - Check for GTest/Catch2
  if (info.language == "cpp" || info.language == "c") {
    if (fs::exists(root + "/CMakeLists.txt")) {
      // Look for enable_testing() or gtest in CMakeLists.txt
      if (file_contains(root + "/CMakeLists.txt", "enable_testing")) {
        info.test_framework = "ctest";
        info.has_tests = true;
      }
      if (file_contains(root + "/CMakeLists.txt", "GTest::")) {
        info.test_framework = "gtest";
        info.has_tests = true;
      }
    }
    // Check for Catch2 header
    if (file_exists_in_tree(root, "catch.hpp") ||
        file_exists_in_tree(root, "catch2/catch.hpp")) {
      info.test_framework = "catch2";
      info.has_tests = true;
    }
  }

  // Python - Check for pytest/unittest
  if (info.language == "python") {
    if (fs::exists(root + "/pytest.ini") ||
        fs::exists(root + "/pyproject.toml")) {
      info.test_framework = "pytest";
      info.has_tests = true;
    } else if (has_files_matching(root, "test_*.py") ||
               has_files_matching(root, "*_test.py")) {
      info.test_framework = "pytest";  // Default for Python
      info.has_tests = true;
    }
  }

  // Rust - cargo test is standard
  if (info.language == "rust" && info.build_system == "cargo") {
    info.test_framework = "cargo";
    info.has_tests = true;  // Rust always supports tests
  }

  // JavaScript/TypeScript
  if (info.language == "javascript" || info.language == "typescript") {
    if (file_contains(root + "/package.json", "jest")) {
      info.test_framework = "jest";
      info.has_tests = true;
    } else if (file_contains(root + "/package.json", "vitest")) {
      info.test_framework = "vitest";
      info.has_tests = true;
    }
  }

  // Go
  if (info.language == "go" && has_files_matching(root, "*_test.go")) {
    info.test_framework = "go";
    info.has_tests = true;
  }

  // Detect test directory
  for (const auto& candidate : {"tests", "test", "__tests__", "spec"}) {
    if (fs::exists(root + "/" + candidate) &&
        fs::is_directory(root + "/" + candidate)) {
      info.test_directory = candidate;
      break;
    }
  }
}
```

### 4.2 Store Test Facts

```cpp
// After detection, store in Nexus
void Detector::store_facts(int64_t project_id, const ProjectInfo& info) {
  // ... existing facts ...

  if (info.has_tests) {
    store_->set_fact(project_id, "detector", "test_framework",
                     info.test_framework);
    store_->set_fact(project_id, "detector", "test_directory",
                     info.test_directory);
  }
}
```

---

## 5. Planning Enhancement

### 5.1 Extend `brain::Plan` Types

```cpp
// modules/brain/include/brain/types.hpp
enum class Action {
  SKIP,
  BUILD,
  TEST,      // NEW: Run tests
  CLEAN,
  UNKNOWN
};

struct Plan {
  Action action = Action::UNKNOWN;
  std::string reason;
  std::string command;
  std::vector<std::string> args;
  std::string working_dir;

  // NEW: Test-specific fields
  std::optional<std::string> test_framework;
  bool run_after_build = false;  // Chain BUILD → TEST
  std::optional<std::string> test_filter;  // Run specific tests
};
```

### 5.2 Planner Decision Logic

```cpp
// modules/brain/src/planner.cpp
Plan Planner::decide(int64_t project_id, const ProjectInfo& info,
                     const SnapshotResult& snapshot) {
  // Existing BUILD logic...

  // If has tests and BUILD succeeded (or no changes), suggest TEST
  if (info.has_tests && snapshot.changed) {
    Plan test_plan = generate_test_plan(info);
    test_plan.reason = "Files changed, tests should be run";
    test_plan.run_after_build = true;  // Run after successful build
    return test_plan;
  }

  // If no changes but tests exist, offer TEST as option
  if (info.has_tests && !snapshot.changed) {
    Plan test_plan = generate_test_plan(info);
    test_plan.reason = "No changes detected, but tests available";
    return test_plan;
  }

  // ... rest of logic
}

Plan Planner::generate_test_plan(const ProjectInfo& info) {
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
  } else if (info.test_framework == "go") {
    plan.command = "go";
    plan.args = {"test", "-v", "./..."};
  }

  return plan;
}
```

---

## 6. Test Runner Implementation

### 6.1 Runner Interface

```cpp
// modules/test/include/test/runner.hpp
namespace nazg::test {

class Runner {
public:
  Runner(nazg::nexus::Store* store, nazg::blackbox::logger* log);

  // Execute tests and return results
  TestRun execute(int64_t project_id,
                  const brain::Plan& plan,
                  const RunOptions& opts = {});

  // Parse existing test output (for post-processing)
  TestRun parse_output(Framework framework,
                       const std::string& output,
                       const std::string& stderr_output);

private:
  std::unique_ptr<Adapter> get_adapter(Framework framework);
  bool parse_coverage(const std::string& coverage_file, Coverage& out);

  nazg::nexus::Store* store_;
  nazg::blackbox::logger* log_;
};

struct RunOptions {
  bool collect_coverage = false;
  std::optional<std::string> filter;  // Run specific test pattern
  int timeout_seconds = 300;
  bool fail_fast = false;             // Stop on first failure
  int parallel_jobs = 1;              // Number of parallel test jobs
};

} // namespace nazg::test
```

### 6.2 Framework Adapters

```cpp
// modules/test/include/test/adapters.hpp
namespace nazg::test {

class Adapter {
public:
  virtual ~Adapter() = default;

  // Build command to execute tests
  virtual std::vector<std::string> build_command(
      const std::string& working_dir,
      const RunOptions& opts) = 0;

  // Parse test output into structured results
  virtual TestRun parse_output(
      const std::string& stdout_output,
      const std::string& stderr_output,
      int exit_code,
      int64_t duration_ms) = 0;

  // Extract coverage if available
  virtual std::optional<Coverage> parse_coverage(
      const std::string& working_dir) = 0;
};

class GTestAdapter : public Adapter {
  // GoogleTest XML output parsing
  std::vector<std::string> build_command(...) override;
  TestRun parse_output(...) override;
  std::optional<Coverage> parse_coverage(...) override;
};

class PytestAdapter : public Adapter {
  // pytest --junitxml output parsing
};

// ... other adapters
} // namespace nazg::test
```

### 6.3 Example: GTest Adapter

```cpp
// modules/test/src/adapters/gtest.cpp
std::vector<std::string> GTestAdapter::build_command(
    const std::string& working_dir,
    const RunOptions& opts) {

  std::vector<std::string> cmd = {"ctest", "--test-dir", "build"};

  cmd.push_back("--output-on-failure");

  if (opts.filter) {
    cmd.push_back("-R");
    cmd.push_back(*opts.filter);
  }

  if (opts.parallel_jobs > 1) {
    cmd.push_back("-j");
    cmd.push_back(std::to_string(opts.parallel_jobs));
  }

  if (opts.collect_coverage) {
    // Enable coverage collection (requires build with coverage flags)
    cmd.push_back("--verbose");
  }

  return cmd;
}

TestRun GTestAdapter::parse_output(const std::string& stdout_output,
                                    const std::string& stderr_output,
                                    int exit_code,
                                    int64_t duration_ms) {
  TestRun run;
  run.framework = Framework::GTEST;
  run.duration_ms = duration_ms;

  // Parse CTest output format or GTest XML
  // Example line: "1: Test command: /path/to/test_binary"
  // Example line: "1/5 Test #1: MyTest.BasicAssertion ..... Passed 0.01 sec"

  std::istringstream stream(stdout_output);
  std::string line;

  while (std::getline(stream, line)) {
    if (line.find("Test #") != std::string::npos) {
      TestCase tc;
      // Parse test name, status, duration
      // ... parsing logic ...
      run.cases.push_back(tc);
    }
  }

  // Count statuses
  for (const auto& tc : run.cases) {
    run.total++;
    switch (tc.status) {
      case TestStatus::PASSED: run.passed++; break;
      case TestStatus::FAILED: run.failed++; break;
      case TestStatus::SKIPPED: run.skipped++; break;
      case TestStatus::ERROR: run.errors++; break;
      default: break;
    }
  }

  return run;
}
```

---

## 7. Database Schema

### 7.1 New Tables

```sql
-- modules/nexus/migrations/00X_add_test_tables.sql

-- Test runs (high-level test execution)
CREATE TABLE test_runs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL,
  framework TEXT NOT NULL,        -- 'gtest', 'pytest', etc.
  timestamp INTEGER NOT NULL,     -- Unix timestamp
  duration_ms INTEGER NOT NULL,
  exit_code INTEGER NOT NULL,
  total_tests INTEGER NOT NULL,
  passed INTEGER NOT NULL,
  failed INTEGER NOT NULL,
  skipped INTEGER NOT NULL,
  errors INTEGER NOT NULL,
  triggered_by TEXT,              -- 'manual', 'auto', 'pre-commit'
  FOREIGN KEY (project_id) REFERENCES projects(id)
);

CREATE INDEX idx_test_runs_project ON test_runs(project_id);
CREATE INDEX idx_test_runs_timestamp ON test_runs(timestamp);

-- Individual test cases
CREATE TABLE test_results (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL,
  suite TEXT,                     -- Test suite/class name
  name TEXT NOT NULL,             -- Test name
  status TEXT NOT NULL,           -- 'passed', 'failed', 'skipped', 'error'
  duration_ms INTEGER,
  message TEXT,                   -- Failure/skip message
  file TEXT,                      -- Source file
  line INTEGER,                   -- Line number
  FOREIGN KEY (run_id) REFERENCES test_runs(id) ON DELETE CASCADE
);

CREATE INDEX idx_test_results_run ON test_results(run_id);
CREATE INDEX idx_test_results_status ON test_results(status);
CREATE INDEX idx_test_results_name ON test_results(name);

-- Test coverage tracking
CREATE TABLE test_coverage (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL,
  file_path TEXT NOT NULL,
  line_coverage REAL,             -- 0.0 - 1.0
  branch_coverage REAL,
  lines_covered INTEGER,
  lines_total INTEGER,
  branches_covered INTEGER,
  branches_total INTEGER,
  FOREIGN KEY (run_id) REFERENCES test_runs(id) ON DELETE CASCADE
);

CREATE INDEX idx_coverage_run ON test_coverage(run_id);
CREATE INDEX idx_coverage_file ON test_coverage(file_path);
```

### 7.2 Store API Extension

```cpp
// modules/nexus/include/nexus/store.hpp
namespace nazg::nexus {

class Store {
public:
  // ... existing methods ...

  // Test runs
  int64_t add_test_run(int64_t project_id, const test::TestRun& run);
  std::optional<test::TestRun> get_test_run(int64_t run_id);
  std::vector<test::TestRun> get_test_runs(int64_t project_id, int limit = 10);
  test::TestRun get_latest_test_run(int64_t project_id);

  // Test results (individual cases)
  bool add_test_results(int64_t run_id, const std::vector<test::TestCase>& cases);
  std::vector<test::TestCase> get_test_results(int64_t run_id);
  std::vector<test::TestCase> get_flaky_tests(int64_t project_id, int runs = 20);

  // Coverage
  bool add_coverage(int64_t run_id, const test::Coverage& coverage);
  std::optional<test::Coverage> get_coverage(int64_id run_id);
  test::Coverage get_coverage_trend(int64_t project_id, int days = 30);
};

} // namespace nazg::nexus
```

---

## 8. CLI Commands

### 8.1 Command Registration

```cpp
// modules/test/src/commands.cpp
namespace nazg::test {

void register_commands(directive::registry& reg, directive::context& ctx) {
  reg.add("test", "Run project tests", cmd_test);
  reg.add("test-run", "Execute tests with options", cmd_test_run);
  reg.add("test-results", "Show recent test results", cmd_test_results);
  reg.add("test-coverage", "Display coverage statistics", cmd_test_coverage);
  reg.add("test-watch", "Run tests on file changes", cmd_test_watch);
  reg.add("test-flaky", "Detect flaky tests", cmd_test_flaky);
}

} // namespace nazg::test
```

### 8.2 Example: `nazg test` Command

```bash
# Run all tests
nazg test

# Run with coverage
nazg test --coverage

# Run specific test pattern
nazg test --filter="MyTest.*"

# Run tests in parallel
nazg test -j4

# Fail fast (stop on first failure)
nazg test --fail-fast

# Show last test results
nazg test-results

# Show coverage report
nazg test-coverage

# Watch mode
nazg test-watch
```

### 8.3 Implementation

```cpp
int cmd_test(const directive::command_context& cctx,
             const directive::context& ectx) {
  // Detect project
  std::string cwd = get_cwd();
  brain::Detector detector(ectx.store, ectx.log);
  auto info = detector.detect(cwd);

  if (!info.has_tests) {
    std::cerr << "No tests detected in this project\n";
    return 1;
  }

  // Parse options
  test::RunOptions opts;
  for (int i = 2; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--coverage") {
      opts.collect_coverage = true;
    } else if (arg == "--filter" && i + 1 < cctx.argc) {
      opts.filter = cctx.argv[++i];
    } else if (arg == "-j" && i + 1 < cctx.argc) {
      opts.parallel_jobs = std::atoi(cctx.argv[++i]);
    } else if (arg == "--fail-fast") {
      opts.fail_fast = true;
    }
  }

  // Generate test plan
  brain::Planner planner(ectx.store, ectx.log);
  auto plan = planner.generate_test_plan(info);

  // Execute tests
  test::Runner runner(ectx.store, ectx.log);
  int64_t project_id = ectx.store->ensure_project(cwd);

  std::cout << "Running tests using " << info.test_framework << "...\n";
  auto result = runner.execute(project_id, plan, opts);

  // Display results
  std::cout << "\n";
  std::cout << "Tests: " << result.total << " total, ";
  std::cout << result.passed << " passed, ";
  std::cout << result.failed << " failed, ";
  std::cout << result.skipped << " skipped\n";
  std::cout << "Duration: " << (result.duration_ms / 1000.0) << "s\n";

  if (result.coverage) {
    std::cout << "Coverage: "
              << (result.coverage->line_coverage * 100.0) << "% lines\n";
  }

  return result.failed > 0 ? 1 : 0;
}
```

---

## 9. Assistant Integration

### 9.1 Enhanced Assistant Mode

```cpp
// modules/engine/src/runtime.cpp - run_assistant_mode()

// After detecting project info...
if (project_info.has_tests) {
  // Get latest test run
  auto latest_run = ectx.store->get_latest_test_run(project_id);

  if (latest_run) {
    std::string test_status;
    if (latest_run.failed > 0) {
      test_status = std::to_string(latest_run.failed) + " failing";
      assistant.fact("Tests", test_status, prompt::Color::RED);
    } else {
      test_status = "All passing ✓";
      assistant.fact("Tests", test_status, prompt::Color::GREEN);
    }

    // Show coverage if available
    if (latest_run.coverage) {
      float cov = latest_run.coverage->line_coverage * 100.0;
      std::string cov_str = std::to_string(int(cov)) + "%";
      assistant.fact("Coverage", cov_str);
    }
  } else {
    assistant.fact("Tests", "Not run yet");
  }
}

// Add test actions to menu
if (project_info.has_tests) {
  auto latest_run = ectx.store->get_latest_test_run(project_id);

  if (!latest_run || latest_run.failed > 0) {
    actions.push_back("🧪 Run tests");
    action_commands.push_back("test");
  }

  if (latest_run && latest_run.failed > 0) {
    actions.push_back("🔍 View test failures");
    action_commands.push_back("test-results --failed");
  }

  actions.push_back("📊 View test coverage");
  action_commands.push_back("test-coverage");
}
```

---

## 10. Implementation Phases

### Phase 1: Foundation (Week 1-2)
- [ ] Create `modules/test` structure
- [ ] Define core types in `types.hpp`
- [ ] Add test framework detection to `brain::Detector`
- [ ] Extend `brain::Planner` with `Action::TEST`
- [ ] Create Nexus migration for test tables
- [ ] Implement `nexus::Store` test API methods

### Phase 2: Basic Runner (Week 3-4)
- [ ] Implement `test::Runner` core
- [ ] Create GTest adapter (C++ focus)
- [ ] Create pytest adapter (Python focus)
- [ ] Implement output parsing for both
- [ ] Add `nazg test` command
- [ ] Add `nazg test-results` command

### Phase 3: Additional Frameworks (Week 5-6)
- [ ] Implement Cargo adapter (Rust)
- [ ] Implement Jest adapter (JavaScript)
- [ ] Implement Go test adapter
- [ ] Add framework auto-detection refinement
- [ ] Implement test filtering support

### Phase 4: Coverage & Analytics (Week 7-8)
- [ ] Coverage parsing for GTest (gcov/lcov)
- [ ] Coverage parsing for pytest (coverage.py)
- [ ] Coverage parsing for Cargo (tarpaulin)
- [ ] Add `nazg test-coverage` command
- [ ] Implement coverage trending
- [ ] Add coverage visualization

### Phase 5: Advanced Features (Week 9-10)
- [ ] Implement `nazg test-watch` (file watcher)
- [ ] Add flaky test detection
- [ ] Implement test history comparison
- [ ] Add parallel test execution
- [ ] Performance regression detection

### Phase 6: Polish & Integration (Week 11-12)
- [ ] Assistant mode integration
- [ ] Update all documentation
- [ ] Add comprehensive unit tests
- [ ] Integration tests for each adapter
- [ ] Performance optimization
- [ ] Error handling hardening

---

## 11. Testing Strategy

### 11.1 Unit Tests

```cpp
// modules/test/tests/test_detector.cpp
TEST(TestDetector, DetectsGTest) {
  // Create mock project structure
  // Run detector
  // Assert test_framework == "gtest"
}

TEST(GTestAdapter, ParsesOutput) {
  std::string sample_output = R"(
    Test #1: MyTest.BasicTest ................... Passed    0.01 sec
    Test #2: MyTest.FailingTest ................ ***Failed  0.02 sec
  )";

  GTestAdapter adapter;
  auto run = adapter.parse_output(sample_output, "", 1, 30);

  EXPECT_EQ(run.total, 2);
  EXPECT_EQ(run.passed, 1);
  EXPECT_EQ(run.failed, 1);
}
```

### 11.2 Integration Tests

```bash
# tests/integration/test_runner.sh
./nazg test --filter="*" | grep "Tests: 5 total"
./nazg test-results | grep "passed"
```

---

## 12. Documentation Requirements

- [ ] `docs/test.md` - Full module documentation
- [ ] Update `docs/brain.md` - Test detection additions
- [ ] Update `docs/nexus.md` - New tables
- [ ] Update `README.md` - Test features overview
- [ ] Create `docs/test_adapters.md` - Framework-specific guides
- [ ] Add examples to `docs/examples/testing.md`

---

## 13. Future Enhancements (Post v1)

### Test Generation
- AI-assisted test case generation
- Mutation testing integration
- Property-based testing support

### Advanced Analytics
- Test execution time trends
- Most expensive tests
- Test coupling detection
- Dead test detection

### CI/CD Integration
- Export test results to CI formats (JUnit XML, TAP)
- Import CI test results
- Test result comparison across branches

### Interactive Features
- TUI for test selection
- Real-time test output streaming
- Interactive debugger integration (gdb/lldb)

---

## 14. Success Metrics

After implementation, measure:
- **Detection accuracy**: % of projects correctly identified
- **Framework coverage**: # of supported test frameworks
- **Execution reliability**: Test run success rate
- **Performance**: Test execution overhead vs native
- **Developer adoption**: % of users running `nazg test`
- **Coverage trends**: Average coverage change over time

---

## 15. Open Questions

1. **Coverage instrumentation**: Should Nazg rebuild with coverage flags, or assume pre-instrumented binaries?
2. **Test selection**: How to handle test dependencies (e.g., integration tests requiring DB)?
3. **Container support**: Should tests run in isolated containers?
4. **Remote execution**: Leverage bot framework for distributed testing?
5. **Snapshot testing**: Support visual regression and snapshot tests?

---

## 16. References

- GoogleTest XML Output: https://github.com/google/googletest/blob/main/docs/advanced.md#generating-an-xml-report
- pytest JUnit XML: https://docs.pytest.org/en/stable/how-to/output.html#creating-junitxml-format-files
- Cargo test: https://doc.rust-lang.org/cargo/commands/cargo-test.html
- Jest output: https://jestjs.io/docs/cli#--json
- Coverage.py: https://coverage.readthedocs.io/

---

**Document Version:** 1.0
**Last Updated:** 2025-10-06
**Reviewers:** [TBD]
**Status:** Ready for Implementation

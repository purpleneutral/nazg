# Test Runner Implementation Checklist

Quick reference for implementing test runner integration. See `test_runner_design.md` for full details.

---

## Phase 1: Foundation ✓ / ✗

### Module Setup
- [x] Create `modules/test/` directory structure
- [x] Create `modules/test/include/test/` headers
- [x] Create `modules/test/src/` implementation
- [ ] Create `modules/test/CMakeLists.txt`
- [ ] Add test module to root CMakeLists.txt

### Core Types
- [x] Define `Framework` enum in `types.hpp`
- [x] Define `TestCase` struct
- [x] Define `TestRun` struct
- [x] Define `Coverage` struct
- [x] Define `RunOptions` struct

### Brain Integration
- [x] Add `test_framework` field to `brain::ProjectInfo`
- [x] Add `has_tests` field to `brain::ProjectInfo`
- [x] Add `test_directory` field to `brain::ProjectInfo`
- [x] Implement `detect_test_framework()` in `brain::Detector`
- [x] Store test facts in Nexus via `store_facts()`

### Planner Enhancement
- [x] Add `Action::TEST` to enum
- [x] Add test fields to `brain::Plan`
- [x] Implement `generate_test_plan()` in `brain::Planner`
- [ ] Update `decide()` to suggest TEST when appropriate

### Database Schema
- [x] Create migration `004_tests.sql`
- [x] Add `test_runs` table
- [x] Add `test_results` table
- [x] Add `test_coverage` table
- [x] Add indexes for performance

### Nexus API
- [x] Implement `add_test_run()`
- [x] Implement `get_test_run()`
- [x] Implement `get_test_runs()`
- [x] Implement `get_latest_test_run()`
- [x] Implement `add_test_result()`
- [x] Implement `get_test_results()`
- [x] Implement `add_test_coverage()`
- [x] Implement `get_test_coverage_summary()` and `get_test_coverage_files()`

---

## Phase 2: Basic Runner ✓ / ✗

### Runner Core
- [ ] Implement `test::Runner` class
- [ ] Implement `execute()` method
- [ ] Implement adapter selection logic
- [ ] Add timeout handling
- [ ] Add output capture

### Adapter Interface
- [ ] Create `Adapter` base class
- [ ] Define `build_command()` pure virtual
- [ ] Define `parse_output()` pure virtual
- [ ] Define `parse_coverage()` pure virtual

### GTest Adapter
- [ ] Implement `GTestAdapter::build_command()`
- [ ] Implement `GTestAdapter::parse_output()`
- [ ] Parse CTest output format
- [ ] Parse GTest XML if available
- [ ] Implement `GTestAdapter::parse_coverage()` (gcov)

### Pytest Adapter
- [ ] Implement `PytestAdapter::build_command()`
- [ ] Implement `PytestAdapter::parse_output()`
- [ ] Parse pytest JUnit XML output
- [ ] Parse pytest verbose output
- [ ] Implement `PytestAdapter::parse_coverage()` (coverage.py)

### CLI Commands
- [ ] Implement `cmd_test()` handler
- [ ] Parse command-line options (--coverage, --filter, -j)
- [ ] Display test results summary
- [ ] Register commands in `register_commands()`
- [ ] Implement `cmd_test_results()` handler

---

## Phase 3: Additional Frameworks ✓ / ✗

### Cargo Adapter (Rust)
- [ ] Implement `CargoAdapter::build_command()`
- [ ] Implement `CargoAdapter::parse_output()`
- [ ] Parse `cargo test` output format
- [ ] Implement `CargoAdapter::parse_coverage()` (tarpaulin)

### Jest Adapter (JavaScript)
- [ ] Implement `JestAdapter::build_command()`
- [ ] Implement `JestAdapter::parse_output()`
- [ ] Parse Jest JSON output
- [ ] Implement `JestAdapter::parse_coverage()` (Istanbul)

### Go Test Adapter
- [ ] Implement `GoTestAdapter::build_command()`
- [ ] Implement `GoTestAdapter::parse_output()`
- [ ] Parse `go test -v` output
- [ ] Implement `GoTestAdapter::parse_coverage()` (go tool cover)

### Detection Refinement
- [ ] Improve test directory detection
- [ ] Add version detection for frameworks
- [ ] Handle multiple test frameworks in same project
- [ ] Add framework-specific config file detection

### Test Filtering
- [ ] Implement pattern matching for test names
- [ ] Support suite-level filtering
- [ ] Support file-level filtering
- [ ] Add regex filter support

---

## Phase 4: Coverage & Analytics ✓ / ✗

### Coverage Collection
- [ ] GTest: Parse gcov/lcov output
- [ ] Pytest: Parse coverage.py JSON
- [ ] Cargo: Parse tarpaulin JSON
- [ ] Jest: Parse Istanbul coverage
- [ ] Store coverage in Nexus

### Coverage Commands
- [ ] Implement `cmd_test_coverage()` handler
- [ ] Display overall coverage statistics
- [ ] Show per-file coverage breakdown
- [ ] Display coverage trend over time
- [ ] Generate HTML coverage report

### Analytics
- [ ] Calculate test duration trends
- [ ] Identify slowest tests
- [ ] Track test success rate over time
- [ ] Detect coverage regressions
- [ ] Generate analytics events in Nexus

---

## Phase 5: Advanced Features ✓ / ✗

### Watch Mode
- [ ] Implement file watching (inotify/fswatch)
- [ ] Detect relevant file changes
- [ ] Re-run affected tests only
- [ ] Implement `cmd_test_watch()` handler
- [ ] Add debouncing for rapid changes

### Flaky Test Detection
- [ ] Track test result history
- [ ] Calculate flip rate per test
- [ ] Implement `cmd_test_flaky()` handler
- [ ] Display flaky test report
- [ ] Store flaky test markers in Nexus

### Parallel Execution
- [ ] Implement job distribution (-j flag)
- [ ] Aggregate parallel results
- [ ] Handle test isolation
- [ ] Add progress reporting

### Performance Regression
- [ ] Track test execution times
- [ ] Detect duration increases
- [ ] Alert on significant slowdowns
- [ ] Store performance baselines

---

## Phase 6: Polish & Integration ✓ / ✗

### Assistant Integration
- [ ] Display test status in assistant mode
- [ ] Show recent test failures
- [ ] Add "Run tests" action to menu
- [ ] Add "View failures" action when tests fail
- [ ] Show coverage in project facts

### Documentation
- [ ] Write `docs/test.md` module documentation
- [ ] Update `docs/brain.md` with test detection
- [ ] Update `docs/nexus.md` with new tables
- [ ] Update `README.md` with test features
- [ ] Write `docs/test_adapters.md` framework guide
- [ ] Add examples to `docs/examples/testing.md`

### Testing
- [ ] Unit tests for detector
- [ ] Unit tests for each adapter
- [ ] Unit tests for parser
- [ ] Unit tests for runner
- [ ] Integration tests (end-to-end)
- [ ] Smoke tests for each framework

### Error Handling
- [ ] Handle missing test executables gracefully
- [ ] Handle malformed test output
- [ ] Handle timeouts properly
- [ ] Add retry logic for flaky infrastructure
- [ ] Improve error messages

### Performance
- [ ] Optimize output parsing
- [ ] Cache detection results
- [ ] Minimize database round-trips
- [ ] Profile and optimize hot paths

---

## Quick Commands Reference

After implementation, these commands should work:

```bash
# Run all tests
nazg test

# Run with coverage
nazg test --coverage

# Run specific tests
nazg test --filter="MyTest.*"

# Parallel execution
nazg test -j4

# View results
nazg test-results
nazg test-results --failed

# Coverage
nazg test-coverage
nazg test-coverage --html

# Watch mode
nazg test-watch

# Flaky tests
nazg test-flaky

# Brain integration
nazg brain detect    # Should show test_framework
nazg brain plan      # Should suggest TEST action
nazg status          # Should show test status
```

---

## Dependencies

Ensure these are available in your environment:

**C++:**
- GoogleTest (for GTest projects)
- Catch2 (for Catch2 projects)
- gcov/lcov (for C++ coverage)

**Python:**
- pytest
- coverage.py

**Rust:**
- cargo (built-in)
- cargo-tarpaulin (for coverage)

**JavaScript:**
- jest or vitest
- Istanbul (coverage)

**Go:**
- go (built-in testing)
- go tool cover

---

## Key Files to Create

```
modules/test/
├── include/test/
│   ├── types.hpp              # Core data structures
│   ├── detector.hpp           # Framework detection
│   ├── runner.hpp             # Test execution engine
│   ├── adapters.hpp           # Adapter base class
│   ├── parser.hpp             # Output parsing utilities
│   └── commands.hpp           # CLI command registration
├── src/
│   ├── detector.cpp
│   ├── runner.cpp
│   ├── parser.cpp
│   ├── commands.cpp
│   └── adapters/
│       ├── gtest.cpp
│       ├── pytest.cpp
│       ├── cargo.cpp
│       ├── jest.cpp
│       └── go.cpp
└── CMakeLists.txt
```

---

## Validation Checklist

Before considering each phase complete:

- [ ] Code compiles without warnings
- [ ] All unit tests pass
- [ ] Integration tests pass for each supported framework
- [ ] Documentation updated
- [ ] Smoke tests pass
- [ ] Memory leaks checked (valgrind)
- [ ] Performance acceptable (<10% overhead)
- [ ] Error messages are helpful
- [ ] Logs are informative
- [ ] Code reviewed

---

**Status Tracking:**
- Phase 1: ✅ ~95% Complete (CMakeLists pending)
- Phase 2: ⬜ Not Started
- Phase 3: ⬜ Not Started
- Phase 4: ⬜ Not Started
- Phase 5: ⬜ Not Started
- Phase 6: ⬜ Not Started

**Last Updated:** 2025-10-06

Update this file as you progress through implementation!

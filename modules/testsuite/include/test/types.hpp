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
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nazg::test {

// Test framework enumeration
enum class Framework {
  UNKNOWN,
  GTEST,     // Google Test (C++)
  CATCH2,    // Catch2 (C++)
  PYTEST,    // pytest (Python)
  UNITTEST,  // unittest (Python)
  CARGO,     // cargo test (Rust)
  JEST,      // Jest (JavaScript/TypeScript)
  VITEST,    // Vitest (JavaScript/TypeScript)
  GO_TEST,   // go test (Go)
  CTEST,     // CTest (CMake)
};

// Convert framework enum to string
inline std::string framework_to_string(Framework fw) {
  switch (fw) {
  case Framework::GTEST:
    return "gtest";
  case Framework::CATCH2:
    return "catch2";
  case Framework::PYTEST:
    return "pytest";
  case Framework::UNITTEST:
    return "unittest";
  case Framework::CARGO:
    return "cargo";
  case Framework::JEST:
    return "jest";
  case Framework::VITEST:
    return "vitest";
  case Framework::GO_TEST:
    return "go";
  case Framework::CTEST:
    return "ctest";
  default:
    return "unknown";
  }
}

// Convert string to framework enum
inline Framework string_to_framework(const std::string &str) {
  if (str == "gtest")
    return Framework::GTEST;
  if (str == "catch2")
    return Framework::CATCH2;
  if (str == "pytest")
    return Framework::PYTEST;
  if (str == "unittest")
    return Framework::UNITTEST;
  if (str == "cargo")
    return Framework::CARGO;
  if (str == "jest")
    return Framework::JEST;
  if (str == "vitest")
    return Framework::VITEST;
  if (str == "go")
    return Framework::GO_TEST;
  if (str == "ctest")
    return Framework::CTEST;
  return Framework::UNKNOWN;
}

// Test framework metadata
struct TestFrameworkInfo {
  Framework framework;
  std::string name;
  std::string version;                     // e.g., "1.14.0"
  std::string command;                     // Base command to run
  std::vector<std::string> args;           // Default arguments
  std::string test_dir;                    // Where tests live
  std::optional<std::string> config_file;  // pytest.ini, jest.config.js, etc.
};

// Test execution status
enum class TestStatus {
  PASSED,
  FAILED,
  SKIPPED,
  ERROR,    // Test crashed or couldn't run
  TIMEOUT,
};

// Convert status to string
inline std::string status_to_string(TestStatus status) {
  switch (status) {
  case TestStatus::PASSED:
    return "passed";
  case TestStatus::FAILED:
    return "failed";
  case TestStatus::SKIPPED:
    return "skipped";
  case TestStatus::ERROR:
    return "error";
  case TestStatus::TIMEOUT:
    return "timeout";
  }
  return "unknown";
}

// Convert string to status
inline TestStatus string_to_status(const std::string &str) {
  if (str == "passed")
    return TestStatus::PASSED;
  if (str == "failed")
    return TestStatus::FAILED;
  if (str == "skipped")
    return TestStatus::SKIPPED;
  if (str == "error")
    return TestStatus::ERROR;
  if (str == "timeout")
    return TestStatus::TIMEOUT;
  return TestStatus::PASSED;
}

// Individual test case result
struct TestCase {
  std::string name;      // "MyTest.BasicAssertion"
  std::string suite;     // "MyTest"
  TestStatus status;
  int64_t duration_ms;
  std::string message;   // Failure message or skip reason
  std::string file;      // Source file (if parseable)
  int line;              // Line number (if parseable)
};

// File-level coverage information
struct FileCoverage {
  std::string path;
  float line_coverage;                // 0.0 - 1.0
  std::vector<int> covered_lines;
  std::vector<int> uncovered_lines;
};

// Overall coverage information
struct Coverage {
  float line_coverage;     // 0.0 - 1.0
  float branch_coverage;   // 0.0 - 1.0
  int lines_covered;
  int lines_total;
  int branches_covered;
  int branches_total;
  std::unordered_map<std::string, FileCoverage> files;
};

// Complete test run result
struct TestRun {
  int64_t id;             // Nexus row ID
  int64_t project_id;
  Framework framework;
  int64_t timestamp;
  int64_t duration_ms;
  int exit_code;
  int total;
  int passed;
  int failed;
  int skipped;
  int errors;
  std::string triggered_by;  // 'manual', 'auto', 'pre-commit'
  std::vector<TestCase> cases;
  std::optional<Coverage> coverage;
};

// Test execution options
struct RunOptions {
  bool collect_coverage = false;
  std::optional<std::string> filter;  // Run specific test pattern
  int timeout_seconds = 300;
  bool fail_fast = false;             // Stop on first failure
  int parallel_jobs = 1;              // Number of parallel test jobs
  bool verbose = false;               // Verbose output
};

}  // namespace nazg::test

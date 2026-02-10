#include "test/adapters.hpp"
#include "blackbox/logger.hpp"

#include <regex>
#include <sstream>

namespace nazg::test {

GTestAdapter::GTestAdapter(nazg::blackbox::logger *log) : log_(log) {}

std::vector<std::string> GTestAdapter::build_command(const std::string &working_dir,
                                                      const RunOptions &opts) {
  std::vector<std::string> cmd = {"ctest"};

  // Test directory
  cmd.push_back("--test-dir");
  cmd.push_back("build");

  // Output on failure
  cmd.push_back("--output-on-failure");

  // Filter tests
  if (opts.filter) {
    cmd.push_back("-R");
    cmd.push_back(*opts.filter);
  }

  // Parallel execution
  if (opts.parallel_jobs > 1) {
    cmd.push_back("-j");
    cmd.push_back(std::to_string(opts.parallel_jobs));
  }

  // Verbose output
  if (opts.verbose) {
    cmd.push_back("--verbose");
  }

  return cmd;
}

TestRun GTestAdapter::parse_output(const std::string &stdout_output,
                                    const std::string &stderr_output, int exit_code,
                                    int64_t duration_ms) {
  TestRun run;
  run.framework = Framework::GTEST;
  run.exit_code = exit_code;
  run.duration_ms = duration_ms;

  // Parse CTest output format
  // Example lines:
  // "Test #1: MyTest ................................   Passed    0.01 sec"
  // "Test #2: FailingTest ...........................***Failed   0.02 sec"

  std::istringstream stream(stdout_output);
  std::string line;

  // Regex to match CTest output
  std::regex test_line_regex(
      R"(Test\s+#(\d+):\s+(\S+)\s+\.+\s*(\*\*\*)?(\w+)\s+([\d.]+)\s+sec)");

  while (std::getline(stream, line)) {
    std::smatch match;
    if (std::regex_search(line, match, test_line_regex)) {
      TestCase tc;
      tc.name = match[2].str();
      tc.suite = "";  // CTest doesn't provide suite info easily

      std::string status_str = match[4].str();
      double duration_sec = std::stod(match[5].str());
      tc.duration_ms = static_cast<int64_t>(duration_sec * 1000.0);

      if (status_str == "Passed") {
        tc.status = TestStatus::PASSED;
        run.passed++;
      } else if (status_str == "Failed") {
        tc.status = TestStatus::FAILED;
        run.failed++;
      } else if (status_str == "Skipped") {
        tc.status = TestStatus::SKIPPED;
        run.skipped++;
      } else {
        tc.status = TestStatus::ERROR;
        run.errors++;
      }

      run.cases.push_back(tc);
      run.total++;
    }
  }

  // If no tests were parsed, try to extract summary line
  // Example: "100% tests passed, 0 tests failed out of 5"
  if (run.total == 0) {
    std::regex summary_regex(R"((\d+)%\s+tests\s+passed,\s+(\d+)\s+tests\s+failed\s+out\s+of\s+(\d+))");
    std::smatch match;
    if (std::regex_search(stdout_output, match, summary_regex)) {
      run.failed = std::stoi(match[2].str());
      run.total = std::stoi(match[3].str());
      run.passed = run.total - run.failed;
    }
  }

  if (log_) {
    log_->debug("GTestAdapter",
                "Parsed " + std::to_string(run.total) + " tests: " +
                    std::to_string(run.passed) + " passed, " +
                    std::to_string(run.failed) + " failed, " +
                    std::to_string(run.skipped) + " skipped");
  }

  return run;
}

std::optional<Coverage> GTestAdapter::parse_coverage(const std::string &working_dir) {
  // Coverage parsing for GTest would require gcov/lcov integration
  // This is a placeholder for future implementation
  (void)working_dir;
  return std::nullopt;
}

}  // namespace nazg::test

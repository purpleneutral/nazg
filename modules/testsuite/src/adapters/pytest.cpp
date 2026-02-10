#include "test/adapters.hpp"
#include "blackbox/logger.hpp"

#include <regex>
#include <sstream>

namespace nazg::test {

PytestAdapter::PytestAdapter(nazg::blackbox::logger *log) : log_(log) {}

std::vector<std::string> PytestAdapter::build_command(const std::string &working_dir,
                                                       const RunOptions &opts) {
  (void)working_dir;

  std::vector<std::string> cmd = {"pytest"};

  // Verbose output
  cmd.push_back("-v");

  // Short traceback
  cmd.push_back("--tb=short");

  // Filter tests
  if (opts.filter) {
    cmd.push_back("-k");
    cmd.push_back(*opts.filter);
  }

  // Parallel execution (requires pytest-xdist plugin)
  if (opts.parallel_jobs > 1) {
    cmd.push_back("-n");
    cmd.push_back(std::to_string(opts.parallel_jobs));
  }

  // Fail fast
  if (opts.fail_fast) {
    cmd.push_back("-x");
  }

  // Coverage (requires pytest-cov plugin)
  if (opts.collect_coverage) {
    cmd.push_back("--cov");
    cmd.push_back("--cov-report=term");
    cmd.push_back("--cov-report=json");
  }

  return cmd;
}

TestRun PytestAdapter::parse_output(const std::string &stdout_output,
                                     const std::string &stderr_output, int exit_code,
                                     int64_t duration_ms) {
  TestRun run;
  run.framework = Framework::PYTEST;
  run.exit_code = exit_code;
  run.duration_ms = duration_ms;

  // Parse pytest output
  // Example lines:
  // "test_file.py::test_function PASSED                                      [ 50%]"
  // "test_file.py::TestClass::test_method FAILED                             [100%]"

  std::istringstream stream(stdout_output);
  std::string line;

  // Regex to match pytest verbose output
  std::regex test_line_regex(R"(^(.+?)::(.*?)\s+(PASSED|FAILED|SKIPPED|ERROR))");

  while (std::getline(stream, line)) {
    std::smatch match;
    if (std::regex_search(line, match, test_line_regex)) {
      TestCase tc;
      tc.file = match[1].str();
      tc.name = match[2].str();

      // Extract suite from name if it contains ::
      size_t suite_pos = tc.name.find("::");
      if (suite_pos != std::string::npos) {
        tc.suite = tc.name.substr(0, suite_pos);
        tc.name = tc.name.substr(suite_pos + 2);
      }

      std::string status_str = match[3].str();

      if (status_str == "PASSED") {
        tc.status = TestStatus::PASSED;
        run.passed++;
      } else if (status_str == "FAILED") {
        tc.status = TestStatus::FAILED;
        run.failed++;
      } else if (status_str == "SKIPPED") {
        tc.status = TestStatus::SKIPPED;
        run.skipped++;
      } else if (status_str == "ERROR") {
        tc.status = TestStatus::ERROR;
        run.errors++;
      }

      run.cases.push_back(tc);
      run.total++;
    }
  }

  // Parse summary line
  // Example: "===== 3 passed, 1 failed in 2.34s ====="
  std::regex summary_regex(
      R"(=+\s*(\d+)\s+passed(?:,\s+(\d+)\s+failed)?(?:,\s+(\d+)\s+skipped)?.*?in\s+([\d.]+)s)");
  std::smatch match;

  std::string combined = stdout_output + stderr_output;
  if (std::regex_search(combined, match, summary_regex)) {
    if (run.total == 0) {
      // If we didn't parse individual tests, use summary
      run.passed = std::stoi(match[1].str());
      if (match[2].matched) {
        run.failed = std::stoi(match[2].str());
      }
      if (match[3].matched) {
        run.skipped = std::stoi(match[3].str());
      }
      run.total = run.passed + run.failed + run.skipped + run.errors;
    }
  }

  if (log_) {
    log_->debug("PytestAdapter",
                "Parsed " + std::to_string(run.total) + " tests: " +
                    std::to_string(run.passed) + " passed, " +
                    std::to_string(run.failed) + " failed, " +
                    std::to_string(run.skipped) + " skipped");
  }

  return run;
}

std::optional<Coverage> PytestAdapter::parse_coverage(const std::string &working_dir) {
  // Coverage parsing would require reading coverage.json file
  // This is a placeholder for future implementation
  (void)working_dir;
  return std::nullopt;
}

}  // namespace nazg::test

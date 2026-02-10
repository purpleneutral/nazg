#pragma once

#include "test/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nazg {
namespace blackbox {
class logger;
}
}  // namespace nazg

namespace nazg::test {

// Base adapter interface for test frameworks
class Adapter {
 public:
  virtual ~Adapter() = default;

  // Build command to execute tests
  virtual std::vector<std::string> build_command(
      const std::string &working_dir, const RunOptions &opts) = 0;

  // Parse test output into structured results
  virtual TestRun parse_output(const std::string &stdout_output,
                               const std::string &stderr_output, int exit_code,
                               int64_t duration_ms) = 0;

  // Extract coverage if available
  virtual std::optional<Coverage> parse_coverage(
      const std::string &working_dir) = 0;
};

// GoogleTest/CTest adapter
class GTestAdapter : public Adapter {
 public:
  explicit GTestAdapter(nazg::blackbox::logger *log);

  std::vector<std::string> build_command(const std::string &working_dir,
                                         const RunOptions &opts) override;

  TestRun parse_output(const std::string &stdout_output,
                       const std::string &stderr_output, int exit_code,
                       int64_t duration_ms) override;

  std::optional<Coverage> parse_coverage(
      const std::string &working_dir) override;

 private:
  nazg::blackbox::logger *log_;
};

// pytest adapter
class PytestAdapter : public Adapter {
 public:
  explicit PytestAdapter(nazg::blackbox::logger *log);

  std::vector<std::string> build_command(const std::string &working_dir,
                                         const RunOptions &opts) override;

  TestRun parse_output(const std::string &stdout_output,
                       const std::string &stderr_output, int exit_code,
                       int64_t duration_ms) override;

  std::optional<Coverage> parse_coverage(
      const std::string &working_dir) override;

 private:
  nazg::blackbox::logger *log_;
};

// Factory function to create adapters
std::unique_ptr<Adapter> create_adapter(Framework framework,
                                        nazg::blackbox::logger *log);

}  // namespace nazg::test

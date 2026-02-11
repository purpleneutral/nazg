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

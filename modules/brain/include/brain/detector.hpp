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
#include <map>
#include <string>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::brain {

// Detection results
struct ProjectInfo {
  std::string root_path;
  std::string language;           // cpp, python, rust, etc.
  std::string build_system;       // cmake, make, cargo, npm
  std::string scm;                // git, svn, hg, none
  std::vector<std::string> tools; // detected tools
  bool has_cmake = false;
  bool has_makefile = false;
  bool has_git = false;
  // Test framework detection
  std::string test_framework;     // gtest, pytest, cargo, jest, etc.
  bool has_tests = false;
  std::string test_directory;     // tests/, test/, __tests__/, etc.
};

// Detect project characteristics and store as facts
class Detector {
public:
  explicit Detector(nazg::nexus::Store *store, nazg::blackbox::logger *log = nullptr);

  // Run all detectors for a project directory
  ProjectInfo detect(const std::string &root_path);

  // Store detection results as facts
  void store_facts(int64_t project_id, const ProjectInfo &info);

private:
  // Individual detectors
  bool detect_cmake(const std::string &root_path);
  bool detect_git(const std::string &root_path);
  std::string detect_language(const std::string &root_path);
  std::string detect_build_system(const std::string &root_path);
  void detect_test_framework(ProjectInfo &info);

  // Test detection helpers
  bool file_contains(const std::string &path, const std::string &pattern);
  bool file_exists_in_tree(const std::string &root, const std::string &filename);
  bool has_files_matching(const std::string &root, const std::string &pattern);

  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;
};

} // namespace nazg::brain

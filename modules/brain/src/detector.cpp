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

#include "brain/detector.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace nazg::brain {

Detector::Detector(nazg::nexus::Store *store, nazg::blackbox::logger *log)
    : store_(store), log_(log) {}

ProjectInfo Detector::detect(const std::string &root_path) {
  ProjectInfo info;
  info.root_path = root_path;

  // Run all detectors
  info.has_cmake = detect_cmake(root_path);
  info.has_git = detect_git(root_path);
  info.has_makefile = fs::exists(fs::path(root_path) / "Makefile");

  info.language = detect_language(root_path);
  info.build_system = detect_build_system(root_path);
  info.scm = info.has_git ? "git" : "none";

  // Detect test framework
  detect_test_framework(info);

  // Build tools list
  if (info.has_cmake) info.tools.push_back("cmake");
  if (info.has_makefile) info.tools.push_back("make");
  if (info.has_git) info.tools.push_back("git");

  if (log_) {
    log_->info("Brain", "Detected project: lang=" + info.language +
                        ", build=" + info.build_system +
                        ", scm=" + info.scm +
                        (info.has_tests ? ", tests=" + info.test_framework : ""));
  }

  return info;
}

void Detector::store_facts(int64_t project_id, const ProjectInfo &info) {
  if (!store_) return;

  store_->set_fact(project_id, "detector", "language", info.language);
  store_->set_fact(project_id, "detector", "build_system", info.build_system);
  store_->set_fact(project_id, "detector", "scm", info.scm);
  store_->set_fact(project_id, "detector", "has_cmake", info.has_cmake ? "true" : "false");
  store_->set_fact(project_id, "detector", "has_git", info.has_git ? "true" : "false");

  // Store tools as comma-separated list
  std::string tools_str;
  for (size_t i = 0; i < info.tools.size(); ++i) {
    if (i > 0) tools_str += ",";
    tools_str += info.tools[i];
  }
  if (!tools_str.empty()) {
    store_->set_fact(project_id, "detector", "tools", tools_str);
  }

  // Store test framework info
  if (info.has_tests) {
    store_->set_fact(project_id, "detector", "has_tests", "true");
    store_->set_fact(project_id, "detector", "test_framework", info.test_framework);
    if (!info.test_directory.empty()) {
      store_->set_fact(project_id, "detector", "test_directory", info.test_directory);
    }
  }

  if (log_) {
    int fact_count = 4 + (info.has_tests ? 3 : 0);
    log_->debug("Brain", "Stored " + std::to_string(fact_count) + " facts");
  }
}

bool Detector::detect_cmake(const std::string &root_path) {
  return fs::exists(fs::path(root_path) / "CMakeLists.txt");
}

bool Detector::detect_git(const std::string &root_path) {
  return fs::exists(fs::path(root_path) / ".git");
}

std::string Detector::detect_language(const std::string &root_path) {
  std::map<std::string, int> ext_counts;

  try {
    for (const auto &entry : fs::recursive_directory_iterator(root_path)) {
      if (!entry.is_regular_file()) continue;

      // Skip common build/dependency directories
      std::string path_str = entry.path().string();
      if (path_str.find("/build/") != std::string::npos ||
          path_str.find("/node_modules/") != std::string::npos ||
          path_str.find("/.git/") != std::string::npos ||
          path_str.find("/target/") != std::string::npos ||
          path_str.find("/dist/") != std::string::npos ||
          path_str.find("/__pycache__/") != std::string::npos ||
          path_str.find("/.cache/") != std::string::npos) {
        continue;
      }

      auto ext = entry.path().extension().string();
      if (ext.empty()) continue;

      ext_counts[ext]++;
    }
  } catch (const std::exception &e) {
    if (log_) {
      log_->warn("Brain", std::string("Language detection error: ") + e.what());
    }
    return "unknown";
  }

  // Determine primary language by file count
  std::pair<std::string, int> max_ext{"", 0};
  for (const auto &[ext, count] : ext_counts) {
    if (count > max_ext.second) {
      max_ext = {ext, count};
    }
  }

  // Aggregate counts for related extensions (group by language)
  int cpp_count = ext_counts[".cpp"] + ext_counts[".cc"] + ext_counts[".cxx"] +
                  ext_counts[".hpp"] + ext_counts[".hh"] + ext_counts[".hxx"];
  int c_count = ext_counts[".c"] + ext_counts[".h"];
  int py_count = ext_counts[".py"];
  int rs_count = ext_counts[".rs"];
  int go_count = ext_counts[".go"];
  int js_count = ext_counts[".js"] + ext_counts[".ts"] + ext_counts[".jsx"] + ext_counts[".tsx"];

  // Find the language with the most files
  int max_count = 0;
  std::string language = "unknown";

  if (cpp_count > max_count) { max_count = cpp_count; language = "cpp"; }
  if (c_count > max_count && c_count > cpp_count) { max_count = c_count; language = "c"; }
  if (py_count > max_count) { max_count = py_count; language = "python"; }
  if (rs_count > max_count) { max_count = rs_count; language = "rust"; }
  if (go_count > max_count) { max_count = go_count; language = "go"; }
  if (js_count > max_count) { max_count = js_count; language = "javascript"; }

  return language;
}

std::string Detector::detect_build_system(const std::string &root_path) {
  if (detect_cmake(root_path)) return "cmake";
  if (fs::exists(fs::path(root_path) / "Makefile")) return "make";
  if (fs::exists(fs::path(root_path) / "Cargo.toml")) return "cargo";
  if (fs::exists(fs::path(root_path) / "package.json")) return "npm";
  if (fs::exists(fs::path(root_path) / "build.gradle")) return "gradle";
  return "unknown";
}

void Detector::detect_test_framework(ProjectInfo &info) {
  const std::string &root = info.root_path;

  // Detect test directory first
  for (const auto &candidate : {"tests", "test", "__tests__", "spec"}) {
    fs::path test_path = fs::path(root) / candidate;
    if (fs::exists(test_path) && fs::is_directory(test_path)) {
      info.test_directory = candidate;
      break;
    }
  }

  // C++ - Check for GTest/Catch2/CTest
  if (info.language == "cpp" || info.language == "c") {
    if (info.has_cmake) {
      std::string cmake_path = root + "/CMakeLists.txt";
      if (file_contains(cmake_path, "enable_testing") ||
          file_contains(cmake_path, "add_test")) {
        info.test_framework = "ctest";
        info.has_tests = true;
      }
      if (file_contains(cmake_path, "GTest::") ||
          file_contains(cmake_path, "gtest")) {
        info.test_framework = "gtest";
        info.has_tests = true;
      }
    }
    // Check for Catch2 header
    if (file_exists_in_tree(root, "catch.hpp") ||
        file_exists_in_tree(root, "catch2")) {
      info.test_framework = "catch2";
      info.has_tests = true;
    }
  }

  // Python - Check for pytest/unittest
  if (info.language == "python") {
    if (fs::exists(fs::path(root) / "pytest.ini") ||
        fs::exists(fs::path(root) / "pyproject.toml")) {
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
  if (info.language == "javascript") {
    std::string pkg_json = root + "/package.json";
    if (file_contains(pkg_json, "jest")) {
      info.test_framework = "jest";
      info.has_tests = true;
    } else if (file_contains(pkg_json, "vitest")) {
      info.test_framework = "vitest";
      info.has_tests = true;
    }
  }

  // Go
  if (info.language == "go" && has_files_matching(root, "*_test.go")) {
    info.test_framework = "go";
    info.has_tests = true;
  }

  if (log_ && info.has_tests) {
    log_->debug("Brain", "Detected test framework: " + info.test_framework +
                         (info.test_directory.empty() ? "" : " in " + info.test_directory));
  }
}

bool Detector::file_contains(const std::string &path, const std::string &pattern) {
  std::ifstream file(path);
  if (!file.is_open()) return false;

  std::string line;
  while (std::getline(file, line)) {
    if (line.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool Detector::file_exists_in_tree(const std::string &root,
                                    const std::string &filename) {
  try {
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
      if (entry.is_regular_file()) {
        std::string name = entry.path().filename().string();
        if (name.find(filename) != std::string::npos) {
          return true;
        }
      }
    }
  } catch (const std::exception &) {
    return false;
  }
  return false;
}

bool Detector::has_files_matching(const std::string &root,
                                   const std::string &pattern) {
  try {
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file()) continue;

      // Skip build directories
      std::string path_str = entry.path().string();
      if (path_str.find("/build/") != std::string::npos ||
          path_str.find("/node_modules/") != std::string::npos ||
          path_str.find("/.git/") != std::string::npos) {
        continue;
      }

      std::string filename = entry.path().filename().string();

      // Simple pattern matching (supports test_*.py and *_test.py)
      if (pattern.front() == '*' && pattern.back() != '*') {
        // Pattern: *suffix
        std::string suffix = pattern.substr(1);
        if (filename.size() >= suffix.size() &&
            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
          return true;
        }
      } else if (pattern.front() != '*' && pattern.back() == '*') {
        // Pattern: prefix*
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        if (filename.compare(0, prefix.size(), prefix) == 0) {
          return true;
        }
      } else if (filename == pattern) {
        return true;
      }
    }
  } catch (const std::exception &) {
    return false;
  }
  return false;
}

} // namespace nazg::brain

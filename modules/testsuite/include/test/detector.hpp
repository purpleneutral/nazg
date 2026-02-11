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

#include <string>

namespace nazg {
namespace blackbox {
class logger;
}
namespace nexus {
class Store;
}
}  // namespace nazg

namespace nazg::test {

// Test framework detection utilities
class Detector {
 public:
  Detector(nazg::nexus::Store *store, nazg::blackbox::logger *log);

  // Detect test framework in the given directory
  TestFrameworkInfo detect(const std::string &root_path);

 private:
  bool file_contains(const std::string &path, const std::string &pattern);
  bool file_exists_in_tree(const std::string &root, const std::string &filename);
  bool has_files_matching(const std::string &root, const std::string &pattern);

  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;
};

}  // namespace nazg::test

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
#include <optional>
#include <string>

namespace nazg::blackbox {
class logger;
}

namespace nazg::scaffold {

enum class Language {
  C,
  CPP,
  PYTHON
};

struct ScaffoldSpec {
  Language lang;
  std::string name;
  std::string root;        // e.g., /home/user/projects or "." for current dir
  bool create_venv = true; // python
  bool use_direnv = true;  // python .envrc
  bool in_place = false;   // if true, scaffold directly in current dir (don't create subdirectory)
};

struct ScaffoldResult {
  bool created = false;
  std::string path; // project root
  std::string message;
};

ScaffoldResult scaffold_project(const ScaffoldSpec &spec,
                                nazg::blackbox::logger *log = nullptr);

} // namespace nazg::scaffold

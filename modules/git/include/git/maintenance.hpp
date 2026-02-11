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
#include <string>

namespace nazg::blackbox {
class logger;
}

namespace nazg::git {

enum class Language { C, CPP, PYTHON, RUST, GO, JAVASCRIPT, UNKNOWN };

// Git maintenance operations
class Maintenance {
public:
  explicit Maintenance(nazg::blackbox::logger *log = nullptr);

  // Generate .gitignore for language
  bool generate_gitignore(Language lang, const std::string &repo_path);

  // Ensure initial commit exists
  bool ensure_initial_commit(const std::string &repo_path,
                             const std::string &message = "Initial commit");

  // Generate .gitattributes
  bool generate_gitattributes(Language lang, const std::string &repo_path);

private:
  nazg::blackbox::logger *log_;

  std::string get_gitignore_template(Language lang) const;
  std::string get_gitattributes_template(Language lang) const;
};

} // namespace nazg::git

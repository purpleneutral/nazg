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

namespace nazg::nexus {
class Store;
}

namespace nazg::git {

// Bare repository management
class BareManager {
public:
  explicit BareManager(nazg::nexus::Store *store,
                       nazg::blackbox::logger *log = nullptr);

  // Create local bare repository
  bool create_bare(const std::string &path);

  // Link working copy to bare repo as origin
  bool link_to_bare(const std::string &work_path, const std::string &bare_path);

  // Clone from bare to working directory
  bool clone_from_bare(const std::string &bare_path,
                       const std::string &work_path);

  // Get default bare path for a project
  std::string default_bare_path(const std::string &project_name) const;

  // Check if path is a bare repository
  bool is_bare_repo(const std::string &path) const;

private:
  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;

  int exec(const std::string &cmd) const;
  std::string exec_output(const std::string &cmd) const;
};

} // namespace nazg::git

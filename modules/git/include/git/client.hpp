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
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::git {

// Git repository state
enum class RepoState {
  NORMAL,
  MERGE,
  REBASE,
  REBASE_INTERACTIVE,
  REBASE_MERGE,
  CHERRY_PICK,
  REVERT,
  BISECT
};

// Git status information
struct Status {
  bool in_repo = false;
  bool has_commits = false;
  bool has_origin = false;
  std::string branch;
  std::optional<std::string> upstream;
  int modified = 0;
  int staged = 0;
  int untracked = 0;
  int conflicted = 0;
  int ahead = 0;
  int behind = 0;
  std::optional<std::string> origin_url;
  RepoState state = RepoState::NORMAL;
  std::vector<std::string> conflicted_files;
};

// Git configuration
struct Config {
  std::optional<std::string> user_name;
  std::optional<std::string> user_email;
  std::optional<std::string> default_branch;
  std::optional<std::string> core_editor;
  std::optional<std::string> pull_rebase;
  std::optional<std::string> push_default;
  std::optional<bool> color_ui;
};

// Git client wrapper for operations
class Client {
public:
  explicit Client(const std::string &repo_path,
                  nazg::blackbox::logger *log = nullptr);

  // Repository initialization
  bool init(const std::string &branch = "main");
  bool is_repo() const;

  // Status and info
  Status status() const;
  Config get_config() const;
  std::optional<std::string> get_origin() const;

  // Configuration management
  std::optional<std::string> get_config_value(const std::string &key, bool global = false) const;
  bool set_config_value(const std::string &key, const std::string &value, bool global = false);
  bool unset_config_value(const std::string &key, bool global = false);
  Config get_full_config(bool global = false) const;

  // Identity management
  bool ensure_identity(const std::string &name, const std::string &email);

  // Basic operations
  bool add(const std::vector<std::string> &files);
  bool add_all();
  bool commit(const std::string &message);
  bool push(const std::string &remote = "origin", const std::string &branch = "");

  // Remote management
  bool add_remote(const std::string &name, const std::string &url);
  bool remove_remote(const std::string &name);
  std::vector<std::pair<std::string, std::string>> list_remotes() const;

  // Utility
  bool has_commits() const;
  std::string current_branch() const;

private:
  std::string repo_path_;
  nazg::blackbox::logger *log_;

  // Execute git command
  int exec(const std::string &cmd) const;
  std::string exec_output(const std::string &cmd) const;
};

} // namespace nazg::git

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

#include "git/client.hpp"
#include "blackbox/logger.hpp"
#include "system/process.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <unistd.h>

namespace nazg::git {

namespace {
// Execute command and return exit code
int exec_cmd(const std::string &cmd) {
  return std::system(cmd.c_str());
}

struct PipeCloser { int operator()(FILE* f) const { return pclose(f); } };

// Execute command and capture output
std::string exec_output(const std::string &cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"),
                                                  PipeCloser{});
  if (!pipe) {
    return "";
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  // Trim trailing newline
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }
  return result;
}

std::string cd_repo_prefix(const std::string &repo_path) {
  return "cd " + ::nazg::system::shell_quote(repo_path) + " && ";
}

// Detect git repository state
RepoState detect_repo_state(const std::string &repo_path) {
  std::string git_dir = repo_path + "/.git";

  // Check for rebase states
  if (std::filesystem::exists(git_dir + "/rebase-merge")) {
    return RepoState::REBASE_MERGE;
  }
  if (std::filesystem::exists(git_dir + "/rebase-apply")) {
    if (std::filesystem::exists(git_dir + "/rebase-apply/rebasing")) {
      return RepoState::REBASE;
    }
    return RepoState::REBASE;
  }

  // Check for merge state
  if (std::filesystem::exists(git_dir + "/MERGE_HEAD")) {
    return RepoState::MERGE;
  }

  // Check for cherry-pick state
  if (std::filesystem::exists(git_dir + "/CHERRY_PICK_HEAD")) {
    return RepoState::CHERRY_PICK;
  }

  // Check for revert state
  if (std::filesystem::exists(git_dir + "/REVERT_HEAD")) {
    return RepoState::REVERT;
  }

  // Check for bisect state
  if (std::filesystem::exists(git_dir + "/BISECT_LOG")) {
    return RepoState::BISECT;
  }

  return RepoState::NORMAL;
}
} // namespace

Client::Client(const std::string &repo_path, nazg::blackbox::logger *log)
    : repo_path_(repo_path), log_(log) {}

bool Client::init(const std::string &branch) {
  std::string cmd = cd_repo_prefix(repo_path_) + "git init";
  if (!branch.empty()) {
    cmd += " -b " + ::nazg::system::shell_quote(branch);
  }
  if (log_) {
    log_->info("Git", "Initializing repository: " + cmd);
  }
  int rc = exec(cmd);
  return rc == 0;
}

bool Client::is_repo() const {
  std::string cmd =
      cd_repo_prefix(repo_path_) +
      "git rev-parse --is-inside-work-tree >/dev/null 2>&1";
  return exec(cmd) == 0;
}

Status Client::status() const {
  Status st;
  st.in_repo = is_repo();
  if (!st.in_repo) {
    return st;
  }

  st.has_commits = has_commits();
  st.branch = current_branch();
  st.origin_url = get_origin();
  st.has_origin = st.origin_url.has_value();
  st.state = detect_repo_state(repo_path_);

  // Get status counts and branch tracking info
  std::string status_out = exec_output(cd_repo_prefix(repo_path_) +
                                       "git status --porcelain=v2 --branch");
  std::istringstream iss(status_out);
  std::string line;
  while (std::getline(iss, line)) {
    if (line.empty()) {
      continue;
    }
    if (line[0] == '#') {
      if (line.rfind("# branch.head ", 0) == 0) {
        st.branch = line.substr(14);
      } else if (line.rfind("# branch.upstream ", 0) == 0) {
        st.upstream = line.substr(18);
      } else if (line.rfind("# branch.ab ", 0) == 0) {
        // Format: # branch.ab +<ahead> -<behind>
        auto plus = line.find('+');
        auto space_after_plus = plus != std::string::npos ? line.find(' ', plus) : std::string::npos;
        auto minus = line.find('-', space_after_plus != std::string::npos ? space_after_plus : 0);
        if (plus != std::string::npos) {
          try {
            std::string ahead_str = (space_after_plus != std::string::npos)
                                        ? line.substr(plus + 1, space_after_plus - plus - 1)
                                        : line.substr(plus + 1);
            st.ahead = std::stoi(ahead_str);
          } catch (...) {
            st.ahead = 0;
          }
        }
        if (minus != std::string::npos) {
          try {
            st.behind = std::stoi(line.substr(minus + 1));
          } catch (...) {
            st.behind = 0;
          }
        }
      }
      continue;
    }

    if (line[0] == '?') {
      st.untracked++;
      continue;
    }

    if (line[0] == '!') {
      // ignored entry; skip
      continue;
    }

    // Type 'u' indicates unmerged (conflicted) files
    // Format: u <XY> <sub> <m1> <m2> <m3> <mW> <h1> <h2> <h3> <path>
    if (line[0] == 'u') {
      st.conflicted++;
      // Extract the filename (after the 9th space-separated field)
      std::istringstream line_ss(line);
      std::string field;
      int field_count = 0;
      while (line_ss >> field && field_count < 9) {
        field_count++;
      }
      // Remaining text is the filename
      std::string filename;
      std::getline(line_ss, filename);
      if (!filename.empty() && filename[0] == ' ') {
        filename = filename.substr(1);
      }
      if (!filename.empty()) {
        st.conflicted_files.push_back(filename);
      }
      continue;
    }

    if (line.size() >= 4 && (line[0] == '1' || line[0] == '2')) {
      char staged_code = line[2];
      char worktree_code = line[3];
      if (staged_code != '.' && staged_code != ' ') {
        st.staged++;
      }
      if (worktree_code != '.' && worktree_code != ' ') {
        st.modified++;
      }
    }
  }

  if (st.branch.empty()) {
    st.branch = current_branch();
  }

  return st;
}

Config Client::get_config() const {
  Config cfg;
  std::string name = exec_output("git config --global user.name");
  std::string email = exec_output("git config --global user.email");

  if (!name.empty()) {
    cfg.user_name = name;
  }
  if (!email.empty()) {
    cfg.user_email = email;
  }
  return cfg;
}

std::optional<std::string> Client::get_origin() const {
  std::string url =
      exec_output(cd_repo_prefix(repo_path_) +
                  "git remote get-url origin 2>/dev/null");
  if (!url.empty()) {
    return url;
  }
  return std::nullopt;
}

std::optional<std::string> Client::get_config_value(const std::string &key, bool global) const {
  std::string scope = global ? "--global" : "--local";
  std::string cmd = "git config " + scope + " " +
                    ::nazg::system::shell_quote(key) + " 2>/dev/null";
  if (!global && !repo_path_.empty()) {
    cmd = cd_repo_prefix(repo_path_) + cmd;
  }
  std::string value = exec_output(cmd);
  if (!value.empty()) {
    return value;
  }
  return std::nullopt;
}

bool Client::set_config_value(const std::string &key, const std::string &value, bool global) {
  std::string scope = global ? "--global" : "--local";
  std::string cmd = "git config " + scope + " " +
                    ::nazg::system::shell_quote(key) + " " +
                    ::nazg::system::shell_quote(value);
  if (!global && !repo_path_.empty()) {
    cmd = cd_repo_prefix(repo_path_) + cmd;
  }
  if (log_) {
    log_->info("Git", "Setting " + key + "=" + value + " (" + (global ? "global" : "local") + ")");
  }
  return exec(cmd) == 0;
}

bool Client::unset_config_value(const std::string &key, bool global) {
  std::string scope = global ? "--global" : "--local";
  std::string cmd = "git config " + scope + " --unset " +
                    ::nazg::system::shell_quote(key);
  if (!global && !repo_path_.empty()) {
    cmd = cd_repo_prefix(repo_path_) + cmd;
  }
  if (log_) {
    log_->info("Git", "Unsetting " + key + " (" + (global ? "global" : "local") + ")");
  }
  return exec(cmd) == 0;
}

Config Client::get_full_config(bool global) const {
  Config cfg;

  auto user_name = get_config_value("user.name", global);
  auto user_email = get_config_value("user.email", global);
  auto default_branch = get_config_value("init.defaultBranch", global);
  auto core_editor = get_config_value("core.editor", global);
  auto pull_rebase = get_config_value("pull.rebase", global);
  auto push_default = get_config_value("push.default", global);
  auto color_ui = get_config_value("color.ui", global);

  if (user_name) cfg.user_name = *user_name;
  if (user_email) cfg.user_email = *user_email;
  if (default_branch) cfg.default_branch = *default_branch;
  if (core_editor) cfg.core_editor = *core_editor;
  if (pull_rebase) cfg.pull_rebase = *pull_rebase;
  if (push_default) cfg.push_default = *push_default;
  if (color_ui) {
    cfg.color_ui = (*color_ui == "true" || *color_ui == "auto");
  }

  return cfg;
}

bool Client::ensure_identity(const std::string &name, const std::string &email) {
  bool ok = true;
  auto cfg = get_config();

  if (!cfg.user_name.has_value() || cfg.user_name->empty()) {
    if (log_) {
      log_->info("Git", "Setting user.name=" + name);
    }
    std::string cmd = "git config --global " +
                      ::nazg::system::shell_quote("user.name") + " " +
                      ::nazg::system::shell_quote(name);
    ok &= (exec(cmd) == 0);
  }

  if (!cfg.user_email.has_value() || cfg.user_email->empty()) {
    if (log_) {
      log_->info("Git", "Setting user.email=" + email);
    }
    std::string cmd = "git config --global " +
                      ::nazg::system::shell_quote("user.email") + " " +
                      ::nazg::system::shell_quote(email);
    ok &= (exec(cmd) == 0);
  }

  return ok;
}

bool Client::add(const std::vector<std::string> &files) {
  if (files.empty())
    return true;

  std::string cmd = cd_repo_prefix(repo_path_) + "git add";
  for (const auto &f : files) {
    cmd += " " + ::nazg::system::shell_quote(f);
  }
  return exec(cmd) == 0;
}

bool Client::add_all() {
  std::string cmd = cd_repo_prefix(repo_path_) + "git add -A";
  return exec(cmd) == 0;
}

bool Client::commit(const std::string &message) {
  std::string cmd = cd_repo_prefix(repo_path_) + "git commit -m " +
                    ::nazg::system::shell_quote(message);
  if (log_) {
    log_->info("Git", "Committing: " + message);
  }
  return exec(cmd) == 0;
}

bool Client::push(const std::string &remote, const std::string &branch) {
  std::string cmd = cd_repo_prefix(repo_path_) + "git push";
  if (!remote.empty()) {
    cmd += " " + ::nazg::system::shell_quote(remote);
  }
  if (!branch.empty()) {
    cmd += " " + ::nazg::system::shell_quote(branch);
  } else if (!remote.empty()) {
    cmd += " -u " + ::nazg::system::shell_quote(remote) + " " +
           ::nazg::system::shell_quote(current_branch());
  }

  if (log_) {
    log_->info("Git", "Pushing to " + remote);
  }
  return exec(cmd) == 0;
}

bool Client::add_remote(const std::string &name, const std::string &url) {
  std::string cmd = cd_repo_prefix(repo_path_) + "git remote add " +
                    ::nazg::system::shell_quote(name) + " " +
                    ::nazg::system::shell_quote(url);
  if (log_) {
    log_->info("Git", "Adding remote " + name + " -> " + url);
  }
  return exec(cmd) == 0;
}

bool Client::remove_remote(const std::string &name) {
  std::string cmd = cd_repo_prefix(repo_path_) + "git remote remove " +
                    ::nazg::system::shell_quote(name);
  return exec(cmd) == 0;
}

std::vector<std::pair<std::string, std::string>> Client::list_remotes() const {
  std::vector<std::pair<std::string, std::string>> remotes;
  std::string output =
      exec_output(cd_repo_prefix(repo_path_) + "git remote -v");
  std::istringstream iss(output);
  std::string line;
  while (std::getline(iss, line)) {
    std::istringstream line_ss(line);
    std::string name, url, type;
    if (line_ss >> name >> url >> type) {
      // Only add fetch entries to avoid duplicates
      if (type.find("fetch") != std::string::npos) {
        remotes.push_back({name, url});
      }
    }
  }
  return remotes;
}

bool Client::has_commits() const {
  std::string cmd = cd_repo_prefix(repo_path_) +
                    "git rev-parse HEAD >/dev/null 2>&1";
  return exec(cmd) == 0;
}

std::string Client::current_branch() const {
  std::string branch = exec_output(cd_repo_prefix(repo_path_) +
                                   "git branch --show-current 2>/dev/null");
  if (branch.empty()) {
    branch = "main"; // default
  }
  return branch;
}

int Client::exec(const std::string &cmd) const {
  return exec_cmd(cmd);
}

std::string Client::exec_output(const std::string &cmd) const {
  return ::nazg::git::exec_output(cmd);
}

} // namespace nazg::git

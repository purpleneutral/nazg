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

#include "git/bare.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <unistd.h>

namespace fs = std::filesystem;

namespace nazg::git {

namespace {
int exec_cmd(const std::string &cmd) { return std::system(cmd.c_str()); }

struct PipeCloser { int operator()(FILE* f) const { return pclose(f); } };

std::string exec_output(const std::string &cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"), PipeCloser{});
  if (!pipe)
    return "";
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }
  return result;
}

std::string get_home() {
  if (const char *home = std::getenv("HOME")) {
    return home;
  }
  return "/tmp";
}
} // namespace

BareManager::BareManager(nazg::nexus::Store *store,
                         nazg::blackbox::logger *log)
    : store_(store), log_(log) {}

bool BareManager::create_bare(const std::string &path) {
  if (is_bare_repo(path)) {
    if (log_) {
      log_->info("Git", "Bare repo already exists: " + path);
    }
    return true;
  }

  // Create parent directories
  fs::path p(path);
  if (p.has_parent_path()) {
    fs::create_directories(p.parent_path());
  }

  // Remove if exists and not a bare repo
  if (fs::exists(path)) {
    fs::remove_all(path);
  }

  if (log_) {
    log_->info("Git", "Creating bare repository: " + path);
  }

  std::string cmd = "git init --bare \"" + path + "\"";
  return exec(cmd) == 0;
}

bool BareManager::link_to_bare(const std::string &work_path,
                                const std::string &bare_path) {
  if (!is_bare_repo(bare_path)) {
    if (log_) {
      log_->error("Git", "Not a bare repository: " + bare_path);
    }
    return false;
  }

  if (log_) {
    log_->info("Git", "Linking " + work_path + " to bare repo at " + bare_path);
  }

  // Add remote
  std::string cmd = "cd \"" + work_path + "\" && git remote add origin \"" +
                    bare_path + "\" 2>/dev/null";
  exec(cmd); // May fail if origin exists, that's OK

  // Update origin if it already exists
  cmd = "cd \"" + work_path + "\" && git remote set-url origin \"" + bare_path +
        "\"";
  return exec(cmd) == 0;
}

bool BareManager::clone_from_bare(const std::string &bare_path,
                                   const std::string &work_path) {
  if (!is_bare_repo(bare_path)) {
    if (log_) {
      log_->error("Git", "Not a bare repository: " + bare_path);
    }
    return false;
  }

  if (log_) {
    log_->info("Git", "Cloning from " + bare_path + " to " + work_path);
  }

  std::string cmd = "git clone \"" + bare_path + "\" \"" + work_path + "\"";
  return exec(cmd) == 0;
}

std::string BareManager::default_bare_path(const std::string &project_name) const {
  std::string base = get_home() + "/clones";
  fs::create_directories(base);
  return base + "/" + project_name + ".git";
}

bool BareManager::is_bare_repo(const std::string &path) const {
  if (!fs::exists(path)) {
    return false;
  }
  std::string cmd = "cd \"" + path + "\" && git rev-parse --is-bare-repository 2>/dev/null";
  std::string result = exec_output(cmd);
  return result == "true";
}

int BareManager::exec(const std::string &cmd) const { return exec_cmd(cmd); }

std::string BareManager::exec_output(const std::string &cmd) const {
  return ::nazg::git::exec_output(cmd);
}

} // namespace nazg::git

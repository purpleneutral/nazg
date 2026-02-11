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
#include "git/server.hpp"
#include <string>
#include <memory>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::git {

// cgit-specific server implementation
class CgitServer : public Server {
public:
  CgitServer(const ServerConfig& cfg,
             nazg::nexus::Store* store,
             nazg::blackbox::logger* log);

  ~CgitServer() override = default;

  bool is_installed() override;
  bool install() override;
  bool configure() override;
  bool sync_repos(const std::vector<std::string>& local_paths) override;
  ServerStatus get_status() override;
  ServerConfig config() const override { return config_; }

private:
  ServerConfig config_;
  nazg::nexus::Store* store_;
  nazg::blackbox::logger* log_;

  // SSH helpers
  bool ssh_exec(const std::string& cmd, std::string* output = nullptr);
  bool ssh_test_connection();
  bool upload_file(const std::string& local, const std::string& remote);

  // Config generation
  std::string generate_cgitrc(const std::vector<std::string>& repo_names);

  // Installation steps
  bool install_deps();
  bool install_cgit_binary();
  bool setup_web_server();
  bool create_repo_directory();

  // Phase 1: Core infrastructure
  bool setup_fcgiwrap();
  bool verify_fcgiwrap_socket();
  bool setup_git_user();
  bool deploy_ssh_key(const std::string& public_key_path);
  std::string generate_nginx_config();
};

} // namespace nazg::git

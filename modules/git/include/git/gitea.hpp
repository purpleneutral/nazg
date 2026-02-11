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

// Forward declaration
class GiteaAPI;

// Gitea-specific server implementation
class GiteaServer : public Server {
public:
  GiteaServer(const ServerConfig& cfg,
              nazg::nexus::Store* store,
              nazg::blackbox::logger* log);

  ~GiteaServer() override;

  // Server interface implementation
  bool is_installed() override;
  bool install() override;
  bool configure() override;
  bool sync_repos(const std::vector<std::string>& local_paths) override;
  ServerStatus get_status() override;
  ServerConfig config() const override { return config_; }

  // Gitea-specific methods
  bool create_user(const std::string& username,
                   const std::string& email,
                   const std::string& password);
  bool create_organization(const std::string& name,
                          const std::string& description = "");
  bool create_repo(const std::string& name,
                   const std::string& owner = "",
                   bool is_private = true);
  bool delete_repo(const std::string& owner, const std::string& repo);
  bool setup_webhook(const std::string& owner,
                     const std::string& repo,
                     const std::string& url);

  // API client access
  GiteaAPI* api() { return api_.get(); }
  const std::string& admin_token() const { return admin_token_; }
  const std::string& admin_password() const { return admin_password_; }

  // Generate API token for admin user
  std::optional<std::string> create_admin_token();

  // Get API base URL
  std::string get_api_url() const;

private:
  ServerConfig config_;
  nazg::nexus::Store* store_;
  nazg::blackbox::logger* log_;
  std::unique_ptr<GiteaAPI> api_;
  std::string admin_token_;
  std::string admin_password_;

  // SSH helpers (following cgit pattern)
  bool ssh_exec(const std::string& cmd, std::string* output = nullptr);
  bool ssh_test_connection();
  bool upload_file(const std::string& local, const std::string& remote);

  // Installation helpers
  bool download_gitea_binary();
  bool create_gitea_user();
  std::string generate_secret_key();
  std::string generate_app_ini();
  bool setup_systemd_service();
  bool initialize_database();

  // Helper to get SSH connection string
  std::string ssh_connection() const;
};

} // namespace nazg::git

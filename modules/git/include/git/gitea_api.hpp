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
#include <vector>
#include <optional>
#include <cstdint>

namespace nazg::blackbox {
class logger;
}

namespace nazg::git {

// Gitea API structures
struct User {
  int64_t id = 0;
  std::string username;
  std::string email;
  std::string full_name;
  bool is_admin = false;
};

struct Organization {
  int64_t id = 0;
  std::string username;        // org login name
  std::string full_name;       // display name
  std::string description;
  std::string website;
};

struct Repository {
  int64_t id = 0;
  std::string name;
  std::string full_name;       // owner/repo
  std::string description;
  bool is_private = true;
  std::string clone_url;       // HTTP(S) URL
  std::string ssh_url;         // SSH URL
  std::string html_url;        // Web UI URL
};

struct Webhook {
  int64_t id = 0;
  std::string type;            // "gitea" or "gogs"
  std::string url;
  std::string content_type;    // "json" or "form"
  std::vector<std::string> events;
  bool active = true;
};

// Gitea API client
class GiteaAPI {
public:
  GiteaAPI(const std::string& base_url,
           const std::string& token,
           nazg::blackbox::logger* log = nullptr);

  ~GiteaAPI();

  // User management
  std::optional<User> get_user(const std::string& username);
  bool create_user(const User& user, const std::string& password);
  std::vector<User> list_users();

  // Organization management
  bool create_org(const Organization& org);
  std::vector<Organization> list_orgs();
  std::optional<Organization> get_org(const std::string& orgname);

  // Repository management
  bool create_repo(const std::string& owner, const Repository& repo);
  bool delete_repo(const std::string& owner, const std::string& repo);
  std::optional<Repository> get_repo(const std::string& owner,
                                      const std::string& repo);
  std::vector<Repository> list_repos(const std::string& owner = "");

  // Webhook management
  bool create_webhook(const std::string& owner,
                     const std::string& repo,
                     const Webhook& hook);
  std::vector<Webhook> list_webhooks(const std::string& owner,
                                     const std::string& repo);
  bool delete_webhook(const std::string& owner,
                     const std::string& repo,
                     int64_t hook_id);

  // Mirror management
  bool mirror_repo(const std::string& remote_url,
                   const std::string& name,
                   bool is_private = true);

  // SSH key management
  struct SSHKey {
    int64_t id = 0;
    std::string title;
    std::string key;
    std::string fingerprint;
  };

  bool add_ssh_key(const std::string& title, const std::string& key);
  std::vector<SSHKey> list_ssh_keys();
  bool delete_ssh_key(int64_t key_id);

  // Health check
  bool ping();

private:
  std::string base_url_;
  std::string token_;
  nazg::blackbox::logger* log_;

  // HTTP methods
  std::string http_get(const std::string& endpoint);
  std::string http_post(const std::string& endpoint, const std::string& body);
  std::string http_delete(const std::string& endpoint);
  std::string http_patch(const std::string& endpoint, const std::string& body);

  // HTTP helper with full options
  std::string http_request(const std::string& method,
                          const std::string& endpoint,
                          const std::string& body = "");

  // JSON helpers
  std::string repo_to_json(const Repository& repo);
  std::string user_to_json(const User& user, const std::string& password);
  std::string org_to_json(const Organization& org);
  std::string webhook_to_json(const Webhook& hook);

  Repository parse_repo_json(const std::string& json);
  std::vector<Repository> parse_repos_json(const std::string& json);
  User parse_user_json(const std::string& json);
  std::vector<User> parse_users_json(const std::string& json);
  Organization parse_org_json(const std::string& json);
  std::vector<Organization> parse_orgs_json(const std::string& json);
  Webhook parse_webhook_json(const std::string& json);
  std::vector<Webhook> parse_webhooks_json(const std::string& json);

  // JSON utility helpers
  std::string escape_json(const std::string& str);
  std::optional<std::string> extract_json_string(const std::string& json,
                                                  const std::string& key);
  std::optional<int64_t> extract_json_int(const std::string& json,
                                          const std::string& key);
  std::optional<bool> extract_json_bool(const std::string& json,
                                        const std::string& key);
};

} // namespace nazg::git

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

#include "git/cgit.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"
#include "system/process.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>

namespace fs = std::filesystem;

namespace nazg::git {

namespace {

struct PipeCloser { int operator()(FILE* f) const { return pclose(f); } };

// Execute command and capture output
std::string exec_output(const std::string& cmd) {
  std::array<char, 256> buffer;
  std::string result;
  std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"), PipeCloser{});
  if (!pipe) {
    return "";
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

std::string trim(const std::string& s) {
  auto start = s.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  auto end = s.find_last_not_of(" \t\n\r");
  return s.substr(start, end - start + 1);
}

} // namespace

CgitServer::CgitServer(const ServerConfig& cfg,
                       nazg::nexus::Store* store,
                       nazg::blackbox::logger* log)
    : config_(cfg), store_(store), log_(log) {

  // Set defaults
  if (config_.config_path.empty()) {
    config_.config_path = "/etc/cgitrc";
  }
  if (config_.web_url.empty()) {
    config_.web_url = "http://" + config_.host + "/cgit";
  }
}

bool CgitServer::ssh_test_connection() {
  std::string cmd = "ssh -o ConnectTimeout=5 -o BatchMode=yes -o StrictHostKeyChecking=accept-new -p " +
                    std::to_string(config_.ssh_port) + " " +
                    nazg::system::shell_quote(config_.ssh_user + "@" + config_.host) +
                    " 'echo connected' 2>&1";

  if (log_) {
    log_->debug("Git/cgit", "Testing SSH: " + config_.ssh_user + "@" + config_.host + ":" + std::to_string(config_.ssh_port));
    log_->debug("Git/cgit", "SSH command: " + cmd);
  }

  std::string output = exec_output(cmd);
  bool connected = trim(output) == "connected";

  if (!connected && log_) {
    log_->error("Git/cgit", "SSH test failed. Output: " + output);
    log_->info("Git/cgit", "Trying to connect as: " + config_.ssh_user + "@" + config_.host);
    log_->info("Git/cgit", "If this fails, use --ssh-user to specify the correct user");
  }

  return connected;
}

bool CgitServer::ssh_exec(const std::string& cmd, std::string* output) {
  std::string full_cmd = "ssh -o StrictHostKeyChecking=accept-new -p " +
                         std::to_string(config_.ssh_port) + " " +
                         nazg::system::shell_quote(config_.ssh_user + "@" + config_.host) +
                         " " + nazg::system::shell_quote(cmd) + " 2>&1";

  if (log_) {
    log_->debug("Git/cgit", "SSH exec: " + cmd);
  }

  if (output) {
    *output = exec_output(full_cmd);
    return true;
  } else {
    int ret = std::system(full_cmd.c_str());
    return ret == 0;
  }
}

bool CgitServer::upload_file(const std::string& local, const std::string& remote) {
  std::string cmd = "scp -o StrictHostKeyChecking=accept-new -P " +
                    std::to_string(config_.ssh_port) + " " +
                    nazg::system::shell_quote(local) + " " +
                    nazg::system::shell_quote(config_.ssh_user + "@" + config_.host + ":" + remote) +
                    " 2>&1";

  if (log_) {
    log_->debug("Git/cgit", "Uploading: " + local + " -> " + remote);
  }

  int ret = std::system(cmd.c_str());
  return ret == 0;
}

bool CgitServer::is_installed() {
  if (!ssh_test_connection()) {
    if (log_) {
      log_->warn("Git/cgit", "Cannot connect to " + config_.host);
    }
    return false;
  }

  std::string output;
  ssh_exec("which cgit", &output);

  bool installed = !trim(output).empty();

  if (log_) {
    if (installed) {
      log_->info("Git/cgit", "cgit is installed on " + config_.host);
    } else {
      log_->info("Git/cgit", "cgit not found on " + config_.host);
    }
  }

  return installed;
}

ServerStatus CgitServer::get_status() {
  ServerStatus status;
  status.type = "cgit";

  // Test SSH connection
  status.reachable = ssh_test_connection();
  if (!status.reachable) {
    status.error_message = "Cannot SSH to " + config_.host;
    return status;
  }

  // Check if cgit is installed
  std::string output;
  ssh_exec("which cgit", &output);
  status.installed = !trim(output).empty();

  if (status.installed) {
    // Try to get cgit version - cgit may not support --version
    ssh_exec("cgit -v 2>/dev/null || cgit 2>&1 | head -1", &output);
    std::string version_output = trim(output);
    // Only set version if it doesn't look like an error
    if (!version_output.empty() &&
        version_output.find("command not found") == std::string::npos &&
        version_output.find("usage") == std::string::npos) {
      status.version = version_output;
    }

    // Count repos - look for directories ending in .git
    ssh_exec("find " + config_.repo_base_path + " -maxdepth 1 -type d -name '*.git' 2>/dev/null | wc -l", &output);
    status.repo_count = std::atoi(trim(output).c_str());
  }

  return status;
}

std::string CgitServer::generate_cgitrc(const std::vector<std::string>& repo_names) {
  std::ostringstream out;

  out << "# Generated by nazg\n";
  out << "# cgit configuration for " << config_.host << "\n";
  out << "# Do not edit manually - regenerate with: nazg git server configure\n\n";

  out << "# Site info\n";
  out << "root-title=Git Repositories\n";
  out << "root-desc=Self-hosted git server via nazg\n\n";

  out << "# Repository scanning\n";
  out << "scan-path=" << config_.repo_base_path << "\n";
  out << "enable-index-owner=1\n";
  out << "enable-index-links=1\n\n";

  out << "# Clone URLs (SSH + HTTP)\n";
  out << "clone-url=ssh://git@" << config_.host << config_.repo_base_path << "/$CGIT_REPO_URL\n";
  out << "clone-url=http://" << config_.host << "/git/$CGIT_REPO_URL\n\n";

  out << "# Enable HTTP clone support\n";
  out << "enable-http-clone=1\n\n";

  out << "# Caching (improves performance)\n";
  out << "cache-size=1000\n";
  out << "cache-root=/var/cache/cgit\n";
  out << "cache-repo-ttl=5\n";
  out << "cache-root-ttl=5\n";
  out << "cache-static-ttl=60\n\n";

  out << "# Static assets\n";
  out << "css=/cgit-css/cgit.css\n";
  out << "logo=/cgit-css/cgit.png\n";
  out << "favicon=/cgit-css/favicon.ico\n\n";

  out << "# UI settings\n";
  out << "enable-commit-graph=1\n";
  out << "enable-log-filecount=1\n";
  out << "enable-log-linecount=1\n";
  out << "max-stats=quarter\n";
  out << "branch-sort=age\n\n";

  out << "# Security\n";
  out << "snapshots=tar.gz zip\n";
  out << "enable-git-config=0\n\n";

  // Individual repo sections if needed
  if (!repo_names.empty()) {
    out << "# Repository-specific settings\n";
    for (const auto& name : repo_names) {
      std::string display_name = name;
      if (display_name.size() > 4 &&
          display_name.substr(display_name.size() - 4) == ".git") {
        display_name = display_name.substr(0, display_name.size() - 4);
      }
      out << "repo.url=" << display_name << "\n";
      out << "repo.path=" << config_.repo_base_path << "/" << name << "\n";
      out << "repo.desc=" << display_name << " repository\n\n";
    }
  }

  return out.str();
}

bool CgitServer::install_deps() {
  if (log_) {
    log_->info("Git/cgit", "Installing dependencies...");
  }

  // Detect package manager and install
  std::string check_apt = "command -v apt-get >/dev/null 2>&1 && echo apt";
  std::string check_dnf = "command -v dnf >/dev/null 2>&1 && echo dnf";
  std::string check_pacman = "command -v pacman >/dev/null 2>&1 && echo pacman";

  std::string pkg_mgr;
  ssh_exec(check_apt + " || " + check_dnf + " || " + check_pacman, &pkg_mgr);
  pkg_mgr = trim(pkg_mgr);

  if (pkg_mgr.empty()) {
    if (log_) {
      log_->error("Git/cgit", "Unknown package manager");
    }
    return false;
  }

  std::string install_cmd;
  if (pkg_mgr == "apt") {
    install_cmd = "sudo apt-get update && sudo apt-get install -y cgit fcgiwrap nginx";
  } else if (pkg_mgr == "dnf") {
    install_cmd = "sudo dnf install -y cgit fcgiwrap nginx";
  } else if (pkg_mgr == "pacman") {
    install_cmd = "sudo pacman -S --noconfirm cgit fcgiwrap nginx";
  }

  return ssh_exec(install_cmd);
}

bool CgitServer::install_cgit_binary() {
  // Most distros have cgit in repos, so install_deps() handles this
  return true;
}

bool CgitServer::setup_web_server() {
  if (log_) {
    log_->info("Git/cgit", "Configuring web server...");
  }

  // Generate complete nginx configuration
  std::string nginx_conf = generate_nginx_config();

  // Write to temp file
  std::string tmp_conf = "/tmp/nazg-cgit-nginx.conf";
  std::ofstream out(tmp_conf);
  if (!out) {
    if (log_) {
      log_->error("Git/cgit", "Failed to create temp nginx config");
    }
    return false;
  }
  out << nginx_conf;
  out.close();

  // Upload to remote
  if (!upload_file(tmp_conf, "/tmp/nazg-cgit-nginx.conf")) {
    fs::remove(tmp_conf);
    return false;
  }

  // Detect nginx config location (Arch vs Debian)
  std::string output;
  ssh_exec("test -d /etc/nginx/conf.d && echo confd || echo sites", &output);
  bool use_confd = (trim(output) == "confd");

  bool ok;
  if (use_confd) {
    // Arch style: /etc/nginx/conf.d/
    if (log_) {
      log_->debug("Git/cgit", "Using Arch-style nginx config (conf.d/)");
    }
    ok = ssh_exec("sudo mv /tmp/nazg-cgit-nginx.conf /etc/nginx/conf.d/cgit.conf");
  } else {
    // Debian style: /etc/nginx/sites-available/
    if (log_) {
      log_->debug("Git/cgit", "Using Debian-style nginx config (sites-available/)");
    }
    ok = ssh_exec("sudo mv /tmp/nazg-cgit-nginx.conf /etc/nginx/sites-available/cgit");
    ok = ok && ssh_exec("sudo ln -sf /etc/nginx/sites-available/cgit /etc/nginx/sites-enabled/cgit");
  }

  // Test nginx config
  if (ok) {
    ssh_exec("sudo nginx -t 2>&1", &output);
    if (output.find("syntax is ok") != std::string::npos ||
        output.find("test is successful") != std::string::npos) {
      if (log_) {
        log_->info("Git/cgit", "✓ nginx configuration valid");
      }
    } else {
      if (log_) {
        log_->error("Git/cgit", "nginx config test failed:\n" + output);
      }
      ok = false;
    }
  }

  // Restart nginx
  if (ok) {
    ok = ssh_exec("sudo systemctl restart nginx");
    if (ok && log_) {
      log_->info("Git/cgit", "✓ nginx restarted");
    }
  }

  // Clean up local temp
  fs::remove(tmp_conf);

  return ok;
}

bool CgitServer::create_repo_directory() {
  if (log_) {
    log_->info("Git/cgit", "Creating repository directory...");
  }

  std::string cmd = "sudo mkdir -p " + nazg::system::shell_quote(config_.repo_base_path) + " && " +
                    "sudo chown -R " + nazg::system::shell_quote(config_.ssh_user) + ": " +
                    nazg::system::shell_quote(config_.repo_base_path);

  return ssh_exec(cmd);
}

bool CgitServer::install() {
  if (log_) {
    log_->info("Git/cgit", "Installing cgit on " + config_.host + "...");
    log_->info("Git/cgit", "This will install: cgit, nginx, fcgiwrap");
  }

  // Test connection first
  if (!ssh_test_connection()) {
    if (log_) {
      log_->error("Git/cgit", "Cannot connect via SSH");
      log_->error("Git/cgit", "Ensure SSH access is configured and keys are deployed");
    }
    return false;
  }

  // Phase 1: Install packages
  if (!install_deps()) {
    if (log_) {
      log_->error("Git/cgit", "Failed to install dependencies");
    }
    return false;
  }

  // Phase 2: Set up fcgiwrap (socket activation)
  if (!setup_fcgiwrap()) {
    if (log_) {
      log_->error("Git/cgit", "Failed to setup fcgiwrap");
    }
    return false;
  }

  // Phase 3: Create git user account
  if (!setup_git_user()) {
    if (log_) {
      log_->error("Git/cgit", "Failed to setup git user");
    }
    return false;
  }

  // Phase 4: Deploy SSH key for git user
  // Try standard locations for SSH public key
  std::vector<std::string> key_paths = {
    "~/.ssh/id_ed25519.pub",
    "~/.ssh/id_rsa.pub"
  };

  bool key_deployed = false;
  for (const auto& key_path : key_paths) {
    std::string expanded_path = key_path;
    if (key_path[0] == '~') {
      const char* home = std::getenv("HOME");
      if (home) {
        expanded_path = std::string(home) + key_path.substr(1);
      }
    }

    if (fs::exists(expanded_path)) {
      if (log_) {
        log_->info("Git/cgit", "Found SSH key: " + key_path);
      }
      if (deploy_ssh_key(expanded_path)) {
        key_deployed = true;
        break;
      }
    }
  }

  if (!key_deployed) {
    if (log_) {
      log_->warn("Git/cgit", "No SSH key deployed - manual configuration needed");
      log_->warn("Git/cgit", "Run: nazg git server deploy-key <path-to-key>");
    }
  }

  // Phase 5: Create repository directory
  if (!create_repo_directory()) {
    if (log_) {
      log_->error("Git/cgit", "Failed to create repo directory");
    }
    return false;
  }

  // Phase 6: Configure web server (nginx + git-http-backend)
  if (!setup_web_server()) {
    if (log_) {
      log_->error("Git/cgit", "Failed to setup web server");
    }
    return false;
  }

  // Phase 7: Generate and upload cgitrc
  if (!configure()) {
    if (log_) {
      log_->warn("Git/cgit", "Failed to configure cgit (non-fatal)");
    }
  }

  if (log_) {
    log_->info("Git/cgit", "");
    log_->info("Git/cgit", "✓ cgit installation complete!");
    log_->info("Git/cgit", "");
    log_->info("Git/cgit", "  Web UI:     " + config_.web_url);
    log_->info("Git/cgit", "  HTTP clone: http://" + config_.host + "/git/<repo>.git");
    log_->info("Git/cgit", "  SSH push:   git@" + config_.host + ":" + config_.repo_base_path + "/<repo>.git");
    log_->info("Git/cgit", "");
    if (key_deployed) {
      log_->info("Git/cgit", "Next steps:");
      log_->info("Git/cgit", "  1. Create a bare repo: nazg git create-bare <name>");
      log_->info("Git/cgit", "  2. Or sync existing repos: nazg git server sync");
    } else {
      log_->info("Git/cgit", "Next steps:");
      log_->info("Git/cgit", "  1. Deploy SSH key: nazg git server deploy-key ~/.ssh/id_ed25519.pub");
      log_->info("Git/cgit", "  2. Create bare repos: nazg git create-bare <name>");
    }
  }

  return true;
}

bool CgitServer::configure() {
  if (log_) {
    log_->info("Git/cgit", "Configuring cgit...");
  }

  // Discover repositories on the server so clone URLs are accurate.
  std::vector<std::string> repo_names;
  {
    // Prefer data we already know from Nexus when available.
    if (store_) {
      auto stored_paths =
          store_->list_bare_repo_paths_with_prefix(config_.repo_base_path);
      for (const auto &path : stored_paths) {
        fs::path p(path);
        auto name = p.filename().string();
        if (!name.empty()) {
          repo_names.push_back(name);
        }
      }
    }

    if (repo_names.empty()) {
      std::string output;
      // Prefer GNU find but fall back to ls if unavailable.
      const std::string quoted_repo_path =
          nazg::system::shell_quote(config_.repo_base_path);
      std::string repo_discovery_cmd =
          "if command -v find >/dev/null 2>&1; then "
          "find " +
          quoted_repo_path +
          " -maxdepth 1 -type d -name '*.git' -exec basename {} \\;; "
          "else ls -1 " +
          quoted_repo_path + " 2>/dev/null; fi";

      if (ssh_exec(repo_discovery_cmd, &output)) {
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
          std::string name = trim(line);
          if (name.empty()) {
            continue;
          }
          if (name.size() >= 4 &&
              name.substr(name.size() - 4) == ".git") {
            repo_names.push_back(name);
          }
        }
      }

      if (repo_names.empty() && log_) {
        log_->debug("Git/cgit", "No repositories detected at " +
                                    config_.repo_base_path +
                                    " while generating cgitrc");
      }
    }
  }

  if (!repo_names.empty()) {
    std::sort(repo_names.begin(), repo_names.end());
    repo_names.erase(std::unique(repo_names.begin(), repo_names.end()),
                     repo_names.end());
  }

  // Generate cgitrc
  std::string cgitrc = generate_cgitrc(repo_names);

  // Write to temp file
  std::string tmp_path = "/tmp/cgitrc";
  std::ofstream out(tmp_path);
  if (!out) return false;
  out << cgitrc;
  out.close();

  // Upload
  if (!upload_file(tmp_path, "/tmp/cgitrc")) {
    fs::remove(tmp_path);
    return false;
  }

  // Install config
  bool ok = ssh_exec("sudo mv /tmp/cgitrc " + config_.config_path);

  // Clean up
  fs::remove(tmp_path);

  if (log_) {
    if (ok) {
      log_->info("Git/cgit", "✓ Configuration updated");
    } else {
      log_->error("Git/cgit", "Failed to update configuration");
    }
  }

  return ok;
}

bool CgitServer::sync_repos(const std::vector<std::string>& local_paths) {
  if (log_) {
    log_->info("Git/cgit", "Syncing " + std::to_string(local_paths.size()) + " repo(s)...");
  }

  bool all_ok = true;

  for (const auto& local_path : local_paths) {
    fs::path p(local_path);
    std::string repo_name = p.filename().string();
    std::string remote_path = config_.repo_base_path + "/" + repo_name;

    if (log_) {
      log_->debug("Git/cgit", "Syncing: " + repo_name);
    }

    // Use rsync to sync bare repo
    std::string cmd = "rsync -avz --delete -e " +
                      nazg::system::shell_quote("ssh -o StrictHostKeyChecking=accept-new -p " +
                                                std::to_string(config_.ssh_port)) + " " +
                      nazg::system::shell_quote(local_path + "/") + " " +
                      nazg::system::shell_quote(config_.ssh_user + "@" + config_.host + ":" +
                                                remote_path + "/") + " 2>&1";

    int ret = std::system(cmd.c_str());
    if (ret != 0) {
      if (log_) {
        log_->error("Git/cgit", "Failed to sync: " + repo_name);
      }
      all_ok = false;
    } else {
      if (log_) {
        log_->debug("Git/cgit", "✓ Synced: " + repo_name);
      }
    }
  }

  // Reconfigure cgit to pick up new repos
  if (all_ok) {
    configure();
  }

  return all_ok;
}

// ===== Phase 1: Core Infrastructure Implementation =====

bool CgitServer::setup_fcgiwrap() {
  if (log_) {
    log_->info("Git/cgit", "Setting up fcgiwrap...");
  }

  // Check if systemd is available
  std::string output;
  ssh_exec("command -v systemctl >/dev/null 2>&1 && echo systemd", &output);
  bool has_systemd = trim(output) == "systemd";

  if (has_systemd) {
    // Enable and start fcgiwrap.socket (systemd socket activation)
    if (!ssh_exec("sudo systemctl enable fcgiwrap.socket")) {
      if (log_) {
        log_->error("Git/cgit", "Failed to enable fcgiwrap.socket");
      }
      return false;
    }

    if (!ssh_exec("sudo systemctl start fcgiwrap.socket")) {
      if (log_) {
        log_->error("Git/cgit", "Failed to start fcgiwrap.socket");
      }
      return false;
    }

    // Verify it's active
    ssh_exec("systemctl is-active fcgiwrap.socket", &output);
    if (trim(output) != "active") {
      if (log_) {
        log_->error("Git/cgit", "fcgiwrap.socket is not active");
      }
      return false;
    }

    if (log_) {
      log_->info("Git/cgit", "✓ fcgiwrap.socket enabled and active");
    }
  } else {
    // Fallback for systems without systemd
    if (log_) {
      log_->warn("Git/cgit", "systemd not detected, attempting SysV init");
    }

    if (!ssh_exec("sudo service fcgiwrap start")) {
      if (log_) {
        log_->error("Git/cgit", "Failed to start fcgiwrap service");
      }
      return false;
    }
  }

  return verify_fcgiwrap_socket();
}

bool CgitServer::verify_fcgiwrap_socket() {
  if (log_) {
    log_->debug("Git/cgit", "Verifying fcgiwrap socket...");
  }

  std::string output;
  // Check if socket exists and is a socket file
  ssh_exec("test -S /run/fcgiwrap.sock && echo exists", &output);

  if (trim(output) == "exists") {
    if (log_) {
      log_->info("Git/cgit", "✓ fcgiwrap socket found at /run/fcgiwrap.sock");
    }
    return true;
  }

  // Try alternative socket location
  ssh_exec("test -S /var/run/fcgiwrap.socket && echo exists", &output);
  if (trim(output) == "exists") {
    if (log_) {
      log_->info("Git/cgit", "✓ fcgiwrap socket found at /var/run/fcgiwrap.socket");
    }
    return true;
  }

  if (log_) {
    log_->error("Git/cgit", "fcgiwrap socket not found");
  }
  return false;
}

bool CgitServer::setup_git_user() {
  if (log_) {
    log_->info("Git/cgit", "Setting up git user account...");
  }

  // Check if git user already exists
  std::string output;
  ssh_exec("id git >/dev/null 2>&1 && echo exists", &output);

  if (trim(output) == "exists") {
    if (log_) {
      log_->info("Git/cgit", "Git user already exists");
    }
  } else {
    // Create git user with git-shell
    std::string create_user =
      "sudo useradd -r -m -d /srv/git -s /usr/bin/git-shell git 2>&1 || "
      "sudo useradd -r -m -d /srv/git -s /usr/local/bin/git-shell git";

    if (!ssh_exec(create_user)) {
      if (log_) {
        log_->error("Git/cgit", "Failed to create git user");
      }
      return false;
    }

    if (log_) {
      log_->info("Git/cgit", "✓ Created git user");
    }
  }

  // Create .ssh directory with correct permissions
  std::string setup_ssh =
    "sudo -u git mkdir -p /srv/git/.ssh && "
    "sudo -u git chmod 700 /srv/git/.ssh";

  if (!ssh_exec(setup_ssh)) {
    if (log_) {
      log_->error("Git/cgit", "Failed to create .ssh directory");
    }
    return false;
  }

  if (log_) {
    log_->info("Git/cgit", "✓ Git user configured with git-shell");
  }

  return true;
}

bool CgitServer::deploy_ssh_key(const std::string& public_key_path) {
  if (log_) {
    log_->info("Git/cgit", "Deploying SSH key...");
  }

  // Expand tilde in path
  std::string expanded_path = public_key_path;
  if (public_key_path[0] == '~') {
    const char* home = std::getenv("HOME");
    if (home) {
      expanded_path = std::string(home) + public_key_path.substr(1);
    }
  }

  // Read local public key
  std::ifstream key_file(expanded_path);
  if (!key_file) {
    if (log_) {
      log_->error("Git/cgit", "Cannot read SSH key: " + expanded_path);
    }
    return false;
  }

  std::string public_key((std::istreambuf_iterator<char>(key_file)),
                         std::istreambuf_iterator<char>());
  key_file.close();

  // Write key to temporary file
  std::string tmp_key = "/tmp/nazg-git-key.pub";
  std::ofstream tmp_file(tmp_key);
  if (!tmp_file) {
    if (log_) {
      log_->error("Git/cgit", "Cannot create temporary key file");
    }
    return false;
  }
  tmp_file << public_key;
  tmp_file.close();

  // Upload key to remote
  if (!upload_file(tmp_key, "/tmp/nazg-git-key.pub")) {
    fs::remove(tmp_key);
    return false;
  }

  // Append to authorized_keys with proper permissions
  std::string deploy_cmd =
    "sudo -u git sh -c 'cat /tmp/nazg-git-key.pub >> /srv/git/.ssh/authorized_keys' && "
    "sudo -u git chmod 600 /srv/git/.ssh/authorized_keys && "
    "sudo rm /tmp/nazg-git-key.pub";

  bool ok = ssh_exec(deploy_cmd);

  // Clean up local temp file
  fs::remove(tmp_key);

  if (ok && log_) {
    log_->info("Git/cgit", "✓ SSH key deployed to git user");
  } else if (!ok && log_) {
    log_->error("Git/cgit", "Failed to deploy SSH key");
  }

  return ok;
}

std::string CgitServer::generate_nginx_config() {
  std::ostringstream conf;

  conf << "# Generated by nazg\n";
  conf << "# cgit web interface with git-http-backend\n\n";

  conf << "server {\n";
  conf << "    listen 80;\n";
  conf << "    server_name " << config_.host << ";\n";
  conf << "    client_max_body_size 0;\n\n";

  conf << "    # cgit static assets\n";
  conf << "    location /cgit-css/ {\n";
  conf << "        alias /usr/share/cgit/;\n";
  conf << "    }\n\n";

  conf << "    # cgit UI at /cgit\n";
  conf << "    location /cgit {\n";
  conf << "        include fastcgi_params;\n";
  conf << "        fastcgi_param GATEWAY_INTERFACE CGI/1.1;\n";
  conf << "        fastcgi_param SCRIPT_FILENAME /usr/lib/cgit/cgit.cgi;\n";
  conf << "        fastcgi_param QUERY_STRING $args;\n";
  conf << "        fastcgi_param REQUEST_METHOD $request_method;\n";
  conf << "        fastcgi_param CONTENT_TYPE $content_type;\n";
  conf << "        fastcgi_param CONTENT_LENGTH $content_length;\n";
  conf << "        fastcgi_param SCRIPT_NAME /cgit;\n";
  conf << "        fastcgi_param PATH_INFO $uri;\n";
  conf << "        fastcgi_pass unix:/run/fcgiwrap.sock;\n";
  conf << "    }\n\n";

  conf << "    # Git smart HTTP at /git\n";
  conf << "    location ~ ^/git(/.*)$ {\n";
  conf << "        include fastcgi_params;\n";
  conf << "        fastcgi_param GIT_PROJECT_ROOT " << config_.repo_base_path << ";\n";
  conf << "        fastcgi_param GIT_HTTP_EXPORT_ALL \"\";\n";
  conf << "        fastcgi_param PATH_INFO $1;\n";
  conf << "        fastcgi_param SCRIPT_FILENAME /usr/lib/git-core/git-http-backend;\n";
  conf << "        fastcgi_param REQUEST_METHOD $request_method;\n";
  conf << "        fastcgi_param CONTENT_TYPE $content_type;\n";
  conf << "        fastcgi_param CONTENT_LENGTH $content_length;\n";
  conf << "        fastcgi_pass unix:/run/fcgiwrap.sock;\n";
  conf << "    }\n";
  conf << "}\n";

  return conf.str();
}

} // namespace nazg::git

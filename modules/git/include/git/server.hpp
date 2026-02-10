#pragma once
#include <memory>
#include <string>
#include <optional>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::git {

// Server status information
struct ServerStatus {
  bool reachable = false;
  bool installed = false;
  std::string version;
  std::string type;           // 'cgit', 'gitea', etc.
  int repo_count = 0;
  std::string error_message;
};

// Server configuration
struct ServerConfig {
  std::string type;           // 'cgit', 'gitea', 'gitlab'
  std::string host;
  int port = 80;
  int ssh_port = 22;
  std::string ssh_user = "git";
  std::string repo_base_path = "/srv/git";
  std::string config_path;    // Server-specific config location
  std::string web_url;
  std::string admin_token;
};

// Abstract git server interface
class Server {
public:
  virtual ~Server() = default;

  // Check if server software is installed
  virtual bool is_installed() = 0;

  // Install server software on remote host
  virtual bool install() = 0;

  // Generate and apply configuration
  virtual bool configure() = 0;

  // Sync local bare repos to server
  virtual bool sync_repos(const std::vector<std::string>& local_paths) = 0;

  // Get server status
  virtual ServerStatus get_status() = 0;

  // Get configuration
  virtual ServerConfig config() const = 0;
};

// Factory for creating server instances
std::unique_ptr<Server> create_server(
    const ServerConfig& cfg,
    nazg::nexus::Store* store,
    nazg::blackbox::logger* log);

} // namespace nazg::git

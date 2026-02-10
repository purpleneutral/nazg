#include "git/server.hpp"
#include "git/cgit.hpp"
#include "git/gitea.hpp"
#include <stdexcept>

namespace nazg::git {

std::unique_ptr<Server> create_server(
    const ServerConfig& cfg,
    nazg::nexus::Store* store,
    nazg::blackbox::logger* log) {

  if (cfg.type == "cgit") {
    return std::make_unique<CgitServer>(cfg, store, log);
  } else if (cfg.type == "gitea") {
    return std::make_unique<GiteaServer>(cfg, store, log);
  }
  // Future: gitlab, etc.

  throw std::runtime_error("Unknown server type: " + cfg.type);
}

} // namespace nazg::git

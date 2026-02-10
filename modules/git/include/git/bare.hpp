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

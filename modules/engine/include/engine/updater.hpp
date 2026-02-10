#pragma once
#include <optional>
#include <string>

namespace nazg {
  namespace blackbox {
    class logger;
  }
}

namespace nazg::update {

struct Config {
  std::string prefix;
  std::string bin_dir;
  std::string repo_url;
  std::string ref;
  int keep = 3;
  bool dry_run = false;
  bool from_release = false;

  bool prefer_local = true;   // if true, build local tree if present and skip git ops
  bool allow_dirty = true;    // allow uncommitted changes when deriving version
  bool reexec_after = false;  // exec() into the new binary on success
  std::string local_src_hint; // e.g. "$HOME/projects/cpp/nazg" (checked first if set)

  // Logging
  ::nazg::blackbox::logger* log = nullptr;
  bool verbose = false;
};

struct BuildInfo {
  std::string version;     // e.g., "0.2.4" or git short SHA
  std::string commit;      // full commit hash
  std::string src_dir;
  std::string build_dir;
  std::string staged_dir;  // prefix/vX/bin/nazg lives under this
};

struct CheckResult {
  bool update_available = false;
  std::string current_version;
  std::string latest_version;
  std::string latest_ref; // tag/branch/commit used
};

struct UpdateResult {
  bool ok = false;
  std::string activated_version;
  std::string message;
};

Config default_config();
CheckResult check(const Config &cfg);
UpdateResult update_from_source(const Config &cfg);
UpdateResult rollback(const Config& cfg,
                      std::optional<std::string> to_version = std::nullopt);

} // namespace nazg::update

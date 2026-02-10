#include "nexus/config.hpp"
#include "config/config.hpp"
#include "config/parser.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace nazg::nexus {

Config Config::defaults() {
  Config cfg;
  // Expand environment variables in db_path
  cfg.db_path = nazg::config::expand_env_vars(cfg.db_path);

  // If still has unexpanded vars, fall back to default_state_dir()
  if (cfg.db_path.find('$') != std::string::npos) {
    cfg.db_path = (fs::path(nazg::config::default_state_dir()) / "nazg.db")
                      .string();
  }

  return cfg;
}

Config Config::from_config(const nazg::config::store &store) {
  Config cfg = defaults();

  // Load from [nexus] section
  if (store.has("nexus", "db_path")) {
    cfg.db_path =
        nazg::config::expand_env_vars(store.get_string("nexus", "db_path"));
  }

  if (store.has("nexus", "wal_mode")) {
    cfg.wal_mode = store.get_bool("nexus", "wal_mode", true);
  }

  if (store.has("nexus", "slow_query_threshold_ms")) {
    cfg.slow_query_threshold_ms =
        store.get_int("nexus", "slow_query_threshold_ms", 1000);
  }

  if (store.has("nexus", "max_events_per_project")) {
    cfg.max_events_per_project =
        store.get_int("nexus", "max_events_per_project", 500);
  }

  if (store.has("nexus", "max_command_history")) {
    cfg.max_command_history =
        store.get_int("nexus", "max_command_history", 1000);
  }

  if (store.has("nexus", "auto_prune")) {
    cfg.auto_prune = store.get_bool("nexus", "auto_prune", false);
  }

  return cfg;
}

} // namespace nazg::nexus

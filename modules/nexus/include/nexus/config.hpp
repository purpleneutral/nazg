#pragma once
#include <cstdint>
#include <string>

namespace nazg::config {
class store;
}

namespace nazg::nexus {

struct Config {
  // Database path (supports env vars like $XDG_STATE_HOME)
  std::string db_path = "$XDG_STATE_HOME/nazg/nazg.db";

  // SQLite settings
  bool wal_mode = true;

  // Performance
  int64_t slow_query_threshold_ms = 1000;

  // Auto-pruning (0 = disabled)
  int max_events_per_project = 500;    // 0 to disable
  int max_command_history = 1000;      // 0 to disable
  bool auto_prune = false;             // default: manual only

  // Load from config store
  static Config from_config(const nazg::config::store &cfg);

  // Get default config with expanded paths
  static Config defaults();
};

} // namespace nazg::nexus

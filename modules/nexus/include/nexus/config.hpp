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
#include <cstdint>
#include <string>

namespace nazg::config {
class store;
}

namespace nazg::nexus {

struct Config {
  // Database path (supports env vars like $XDG_STATE_HOME)
  std::string db_path = "${XDG_STATE_HOME:-$HOME/.local/state}/nazg/nazg.db";

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

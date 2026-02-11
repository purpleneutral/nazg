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
#include <map>
#include <optional>
#include <string>
#include <vector>

// Forward declaration for optional logging
namespace nazg::blackbox {
class logger;
}

namespace nazg::config {

class store {
public:
  // Load from default location or custom path
  // logger is optional - if provided, config will log its operations
  explicit store(const std::string &path = "",
                 nazg::blackbox::logger *logger = nullptr);

  // Typed getters with defaults
  std::string get_string(const std::string &section, const std::string &key,
                         const std::string &default_val = "") const;
  int get_int(const std::string &section, const std::string &key,
              int default_val = 0) const;
  bool get_bool(const std::string &section, const std::string &key,
                bool default_val = false) const;

  // Check if key exists
  bool has(const std::string &section, const std::string &key) const;

  // Get all keys in a section
  std::vector<std::string> keys(const std::string &section) const;

  // Reload from disk
  void reload();

  // Set logger after construction (optional)
  void set_logger(nazg::blackbox::logger *logger);

private:
  std::string path_;
  std::map<std::string, std::map<std::string, std::string>> data_;
  nazg::blackbox::logger *log_; // Optional, can be nullptr
  void load();
};

// Get default config path: ~/.config/nazg/config.toml
std::string default_config_path();

} // namespace nazg::config

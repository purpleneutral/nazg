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

#include "config/parser.hpp"
#include <cstdlib>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

namespace nazg::config {

std::string expand_env_vars(const std::string &input) {
  std::string result = input;

  // Match ${VAR:-default} or ${VAR} or $VAR
  std::regex env_pattern(R"(\$\{([^}:]+)(?::-([^}]+))?\}|\$([A-Za-z_][A-Za-z0-9_]*))");
  std::smatch match;

  std::string::const_iterator search_start(result.cbegin());
  std::string output;

  while (std::regex_search(search_start, result.cend(), match, env_pattern)) {
    // Add prefix (before match)
    output.append(search_start, match[0].first);

    std::string var_name;
    std::string default_value;

    if (match[3].matched) {
      // $VAR format
      var_name = match[3].str();
    } else {
      // ${VAR} or ${VAR:-default} format
      var_name = match[1].str();
      if (match[2].matched) {
        default_value = match[2].str();
      }
    }

    // Get env variable
    const char *env_val = std::getenv(var_name.c_str());
    if (env_val) {
      output.append(env_val);
    } else if (!default_value.empty()) {
      output.append(default_value);
    }
    // else: variable not set and no default, leave empty

    search_start = match[0].second;
  }

  // Add remaining string after last match
  output.append(search_start, result.cend());

  return output;
}

std::string default_state_dir() {
  // XDG_STATE_HOME or ~/.local/state
  if (const char *xdg = std::getenv("XDG_STATE_HOME")) {
    return fs::path(xdg) / "nazg";
  }
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / ".local" / "state" / "nazg";
  }
  return ".nazg"; // Fallback to CWD
}

std::string default_cache_dir() {
  // XDG_CACHE_HOME or ~/.cache
  if (const char *xdg = std::getenv("XDG_CACHE_HOME")) {
    return fs::path(xdg) / "nazg";
  }
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / ".cache" / "nazg";
  }
  return ".nazg_cache"; // Fallback to CWD
}

std::string default_data_dir() {
  // XDG_DATA_HOME or ~/.local/share
  if (const char *xdg = std::getenv("XDG_DATA_HOME")) {
    return fs::path(xdg) / "nazg";
  }
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / ".local" / "share" / "nazg";
  }
  return ".nazg_data"; // Fallback to CWD
}

std::string default_bin_dir() {
  // Standard user bin directory (no XDG equivalent)
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / ".local" / "bin";
  }
  return "."; // Fallback to CWD
}

} // namespace nazg::config

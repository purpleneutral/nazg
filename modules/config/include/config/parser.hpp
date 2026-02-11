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
#include <string>

namespace nazg::config {

// Expand environment variables in a string
// Supports: $VAR, ${VAR}, ${VAR:-default}
// Returns expanded string
std::string expand_env_vars(const std::string &input);

// Get default data directory path
// Uses $XDG_STATE_HOME or ~/.local/state
std::string default_state_dir();

// Get default cache directory path
// Uses $XDG_CACHE_HOME or ~/.cache
std::string default_cache_dir();

// Get default data directory path
// Uses $XDG_DATA_HOME or ~/.local/share
std::string default_data_dir();

// Get default bin directory path
// Returns ~/.local/bin (standard location, no XDG equivalent)
std::string default_bin_dir();

} // namespace nazg::config

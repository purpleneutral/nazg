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

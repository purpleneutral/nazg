#include "config/config.hpp"
#include "blackbox/logger.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace nazg::config {

std::string default_config_path() {
  // XDG_CONFIG_HOME or ~/.config
  if (const char *xdg = std::getenv("XDG_CONFIG_HOME")) {
    return fs::path(xdg) / "nazg" / "config.toml";
  }
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / ".config" / "nazg" / "config.toml";
  }
  return "config.toml"; // Fallback to CWD
}

store::store(const std::string &path, nazg::blackbox::logger *logger)
    : path_(path.empty() ? default_config_path() : path), log_(logger) {
  load();
}

void store::set_logger(nazg::blackbox::logger *logger) { log_ = logger; }

void store::load() {
  data_.clear();

  if (!fs::exists(path_)) {
    if (log_)
      log_->debug("Config", "Config file not found: " + path_);
    return; // No config file is OK - use defaults
  }

  std::ifstream file(path_);
  if (!file.is_open()) {
    if (log_)
      log_->warn("Config", "Failed to open config file: " + path_);
    return;
  }

  if (log_)
    log_->info("Config", "Loading config from " + path_);

  // Simple TOML parser (handles basic key=value in [sections])
  std::string current_section;
  std::string line;
  int line_num = 0;

  while (std::getline(file, line)) {
    ++line_num;

    // Trim whitespace
    auto start = line.find_first_not_of(" \t\r\n");
    auto end = line.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) {
      continue; // Empty line
    }
    line = line.substr(start, end - start + 1);

    // Skip comments
    if (line.empty() || line[0] == '#') {
      continue;
    }

    // Section header: [section_name]
    if (line[0] == '[' && line.back() == ']') {
      current_section = line.substr(1, line.length() - 2);
      // Trim section name
      auto s = current_section.find_first_not_of(" \t");
      auto e = current_section.find_last_not_of(" \t");
      if (s != std::string::npos) {
        current_section = current_section.substr(s, e - s + 1);
      }
      continue;
    }

    // Key = value
    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      if (log_)
        log_->warn("Config",
                   "Invalid line " + std::to_string(line_num) + ": " + line);
      continue;
    }

    std::string key = line.substr(0, eq_pos);
    std::string value = line.substr(eq_pos + 1);

    // Trim key and value
    auto trim = [](std::string &s) {
      auto start = s.find_first_not_of(" \t");
      auto end = s.find_last_not_of(" \t");
      if (start == std::string::npos) {
        s.clear();
        return;
      }
      s = s.substr(start, end - start + 1);
    };

    trim(key);
    trim(value);

    // Remove quotes from value if present
    if (value.length() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
      value = value.substr(1, value.length() - 2);
    }

    if (current_section.empty()) {
      if (log_)
        log_->warn("Config", "Key without section at line " +
                                 std::to_string(line_num) + ": " + key);
      continue;
    }

    data_[current_section][key] = value;
  }

  if (log_) {
    int total_keys = 0;
    for (const auto &section : data_) {
      total_keys += section.second.size();
    }
    log_->info("Config", "Loaded " + std::to_string(data_.size()) +
                             " sections, " + std::to_string(total_keys) +
                             " keys");
  }
}

void store::reload() { load(); }

bool store::has(const std::string &section, const std::string &key) const {
  auto sec_it = data_.find(section);
  if (sec_it == data_.end())
    return false;
  return sec_it->second.find(key) != sec_it->second.end();
}

std::string store::get_string(const std::string &section, const std::string &key,
                               const std::string &default_val) const {
  auto sec_it = data_.find(section);
  if (sec_it == data_.end())
    return default_val;
  auto key_it = sec_it->second.find(key);
  if (key_it == sec_it->second.end())
    return default_val;
  return key_it->second;
}

int store::get_int(const std::string &section, const std::string &key,
                   int default_val) const {
  std::string val = get_string(section, key, "");
  if (val.empty())
    return default_val;
  try {
    return std::stoi(val);
  } catch (...) {
    if (log_)
      log_->warn("Config", "Failed to parse int for [" + section + "] " + key +
                               " = " + val);
    return default_val;
  }
}

bool store::get_bool(const std::string &section, const std::string &key,
                     bool default_val) const {
  std::string val = get_string(section, key, "");
  if (val.empty())
    return default_val;

  // Lowercase for comparison
  std::string lower = val;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
    return true;
  if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
    return false;

  if (log_)
    log_->warn("Config", "Failed to parse bool for [" + section + "] " + key +
                             " = " + val);
  return default_val;
}

std::vector<std::string> store::keys(const std::string &section) const {
  std::vector<std::string> result;
  auto sec_it = data_.find(section);
  if (sec_it == data_.end())
    return result;

  for (const auto &kv : sec_it->second) {
    result.push_back(kv.first);
  }
  return result;
}

} // namespace nazg::config

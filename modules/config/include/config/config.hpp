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

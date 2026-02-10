#pragma once
#include <optional>
#include <string>
#include <vector>

namespace nazg::prompt {
class Prompt;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::system {

enum class PackageManager {
  PACMAN,    // Arch Linux
  APT,       // Debian/Ubuntu
  DNF,       // Fedora/RHEL
  BREW,      // macOS
  UNKNOWN
};

struct PackageInfo {
  std::string name;
  PackageManager manager;
  std::string install_command;
};

// Detect which package manager is available on the system
PackageManager detect_package_manager();

// Get human-readable name for package manager
std::string package_manager_name(PackageManager pm);

// Check if a package is installed
bool is_package_installed(const std::string& package_name);

// Install a package with user confirmation
// Returns true if installed successfully, false if user declined or installation failed
bool install_package(const std::string& package_name,
                     nazg::prompt::Prompt* prompt = nullptr,
                     nazg::blackbox::logger* log = nullptr);

// Get installation instructions for a package (multi-distro)
std::vector<PackageInfo> get_install_info(const std::string& package_name);

} // namespace nazg::system

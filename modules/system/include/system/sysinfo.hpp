#pragma once
#include <optional>
#include <string>

namespace nazg::system {

struct system_info {
  // OS details
  std::string distro_id;
  std::string distro_version;
  std::string kernel;
  std::string arch;
  std::string pkg_manager;

  // User environment
  std::string user;
  std::string home;
  std::string shell;
  std::string hostname;

  // Hardware
  int cpu_threads = 0;
  unsigned long long mem_total_kb = 0;
  unsigned long long mem_available_kb = 0;
  std::optional<std::string> gpu;

  // Disk (root filesystem)
  unsigned long long disk_total_bytes = 0;
  unsigned long long disk_used_bytes = 0;

  // Tools
  std::string cxx_path;
  std::string cxx_version;
  std::string python_path;
  std::string python_version;
  std::string git_version;

  // System uptime
  double uptime_seconds = 0.0;
};

// Detect all system information
system_info detect_system_info();

} // namespace nazg::system

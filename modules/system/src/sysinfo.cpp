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

#define NAZG_LOG_COMPONENT "System"
#include "system/sysinfo.hpp"
#include "system/fs.hpp"
#include "system/process.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace nazg::system {

namespace {

// Read key=value from file (e.g., /etc/os-release)
std::string read_key_value(const std::string &path, const std::string &key) {
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind(key + "=", 0) == 0) {
      auto value = line.substr(key.size() + 1);
      // Strip quotes
      if (!value.empty() && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
      }
      return value;
    }
  }
  return {};
}

// Get package manager based on distro
std::string detect_pkg_manager(const std::string &distro_id) {
  if (distro_id == "arch")
    return "pacman";
  if (distro_id == "ubuntu" || distro_id == "debian")
    return "apt";
  if (distro_id == "fedora" || distro_id == "rhel" || distro_id == "centos")
    return "dnf";
  if (distro_id == "opensuse" || distro_id == "sles")
    return "zypper";
  if (distro_id == "alpine")
    return "apk";
  return "unknown";
}

} // namespace

system_info detect_system_info() {
  system_info info;

  // OS information from uname
  struct utsname uts {};
  if (uname(&uts) == 0) {
    info.kernel = uts.release;
    info.arch = uts.machine;
  }

  // Distro information from /etc/os-release
  if (fs::exists("/etc/os-release")) {
    info.distro_id = read_key_value("/etc/os-release", "ID");
    info.distro_version = read_key_value("/etc/os-release", "VERSION_ID");
    info.pkg_manager = detect_pkg_manager(info.distro_id);
  }

  // User environment
  if (const char *u = std::getenv("USER"))
    info.user = u;
  if (const char *h = std::getenv("HOME"))
    info.home = h;
  if (const char *s = std::getenv("SHELL"))
    info.shell = s;

  // Hostname
  info.hostname = read_file_line("/etc/hostname");
  if (info.hostname.empty())
    info.hostname = "localhost";
  info.hostname = trim(info.hostname);

  // CPU threads
  info.cpu_threads = std::thread::hardware_concurrency();

  // Memory information from /proc/meminfo
  std::ifstream meminfo("/proc/meminfo");
  std::string line;
  while (std::getline(meminfo, line)) {
    std::istringstream iss(line);
    std::string key;
    unsigned long long value;
    std::string unit;
    if (iss >> key >> value >> unit) {
      if (key == "MemTotal:")
        info.mem_total_kb = value;
      else if (key == "MemAvailable:")
        info.mem_available_kb = value;
    }
  }

  // Disk information (root filesystem)
  struct statvfs st {};
  if (statvfs("/", &st) == 0) {
    info.disk_total_bytes = st.f_blocks * st.f_frsize;
    unsigned long long free_bytes = st.f_bfree * st.f_frsize;
    info.disk_used_bytes = info.disk_total_bytes - free_bytes;
  }

  // Uptime
  std::ifstream uptime_file("/proc/uptime");
  if (uptime_file) {
    uptime_file >> info.uptime_seconds;
  }

  // Tool detection
  info.cxx_path = run_capture("which c++ 2>/dev/null");
  if (info.cxx_path.empty())
    info.cxx_path = run_capture("which g++ 2>/dev/null");
  if (!info.cxx_path.empty()) {
    info.cxx_version = run_capture(info.cxx_path + " --version 2>/dev/null | head -n1");
    info.cxx_version = trim(info.cxx_version);
  }

  info.python_path = run_capture("which python3 2>/dev/null");
  if (!info.python_path.empty()) {
    info.python_version = run_capture(info.python_path + " --version 2>&1");
    info.python_version = trim(info.python_version);
  }

  std::string git = run_capture("git --version 2>/dev/null");
  info.git_version = trim(git);

  // GPU detection (best effort)
  if (fs::exists("/usr/bin/lspci")) {
    std::string gpu = run_capture("lspci 2>/dev/null | grep -i -E 'vga|3d' | head -n1");
    gpu = trim(gpu);
    if (!gpu.empty())
      info.gpu = gpu;
  }

  return info;
}

} // namespace nazg::system

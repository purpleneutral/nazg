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

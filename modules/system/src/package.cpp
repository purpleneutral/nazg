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

#include "system/package.hpp"
#include "blackbox/logger.hpp"
#include "prompt/prompt.hpp"
#include "system/process.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <optional>
#include <sstream>

namespace nazg::system {
namespace {

struct ManagerCommand {
  PackageManager manager;
  const char* binary;
  const char* install_template;
};

std::string substitute_package(const char* tmpl, const std::string& package_name) {
  std::string command{tmpl};
  std::string token{"<pkg>"};
  auto pos = command.find(token);
  if (pos != std::string::npos) {
    command.replace(pos, token.size(), shell_quote(package_name));
  }
  return command;
}

bool command_exists(const std::string& binary) {
  std::string cmd = "command -v " + shell_quote(binary) + " >/dev/null 2>&1";
  return run_command(cmd) == 0;
}

const std::array<ManagerCommand, 4> kManagerCommands = {{{PackageManager::PACMAN, "pacman", "sudo pacman -S --noconfirm <pkg>"},
                                                         {PackageManager::APT, "apt", "sudo apt install -y <pkg>"},
                                                         {PackageManager::DNF, "dnf", "sudo dnf install -y <pkg>"},
                                                         {PackageManager::BREW, "brew", "brew install <pkg>"}}};

void log_message(nazg::blackbox::logger* log,
                 const std::string& category,
                 const std::string& message) {
  if (!log) {
    return;
  }
  log->info(category, message);
}

void log_error(nazg::blackbox::logger* log,
               const std::string& category,
               const std::string& message) {
  if (!log) {
    return;
  }
  log->error(category, message);
}

} // namespace

PackageManager detect_package_manager() {
  for (const auto& candidate : kManagerCommands) {
    if (command_exists(candidate.binary)) {
      return candidate.manager;
    }
  }
  return PackageManager::UNKNOWN;
}

std::string package_manager_name(PackageManager pm) {
  switch (pm) {
    case PackageManager::PACMAN:
      return "pacman";
    case PackageManager::APT:
      return "apt";
    case PackageManager::DNF:
      return "dnf";
    case PackageManager::BREW:
      return "brew";
    case PackageManager::UNKNOWN:
    default:
      return "unknown";
  }
}

std::vector<PackageInfo> get_install_info(const std::string& package_name) {
  std::vector<PackageInfo> info;
  info.reserve(kManagerCommands.size());

  for (const auto& candidate : kManagerCommands) {
    info.push_back(PackageInfo{package_name,
                               candidate.manager,
                               substitute_package(candidate.install_template, package_name)});
  }

  return info;
}

bool is_package_installed(const std::string& package_name) {
  const auto pm = detect_package_manager();
  std::string cmd;

  switch (pm) {
    case PackageManager::PACMAN:
      cmd = "pacman -Qi " + shell_quote(package_name) + " >/dev/null 2>&1";
      break;
    case PackageManager::APT:
      cmd = "dpkg -s " + shell_quote(package_name) + " >/dev/null 2>&1";
      break;
    case PackageManager::DNF:
      cmd = "dnf list installed " + shell_quote(package_name) + " >/dev/null 2>&1";
      break;
    case PackageManager::BREW:
      cmd = "brew list --versions " + shell_quote(package_name) + " >/dev/null 2>&1";
      break;
    case PackageManager::UNKNOWN:
    default:
      cmd = "command -v " + shell_quote(package_name) + " >/dev/null 2>&1";
      break;
  }

  return run_command(cmd) == 0;
}

bool install_package(const std::string& package_name,
                     nazg::prompt::Prompt* prompt,
                     nazg::blackbox::logger* log) {
  if (is_package_installed(package_name)) {
    return true;
  }

  const auto pm = detect_package_manager();
  const auto install_info = get_install_info(package_name);
  auto match = std::find_if(install_info.begin(), install_info.end(),
                            [pm](const PackageInfo& info) { return info.manager == pm; });

  if (match == install_info.end()) {
    std::ostringstream oss;
    oss << "Unable to detect supported package manager for '" << package_name << "'.";
    log_error(log, "system.package", oss.str());
    std::cerr << oss.str() << "\n";
    return false;
  }

  const std::string install_cmd = match->install_command;
  const std::string pm_name = package_manager_name(pm);

  bool approved = false;

  if (prompt) {
    prompt->title("Package Installation")
        .question("Install package '" + package_name + "'?")
        .info("Detected manager: " + pm_name)
        .action("Command: " + install_cmd);
    approved = prompt->confirm();
  } else {
    std::cout << "Package '" << package_name << "' is required.\n";
    std::cout << "Detected manager: " << pm_name << "\n";
    std::cout << "Command: " << install_cmd << "\n";
    std::cout << "Proceed? [Y/n]: " << std::flush;
    std::string response;
    std::getline(std::cin, response);
    if (response.empty() || response == "y" || response == "Y" || response == "yes" || response == "YES") {
      approved = true;
    }
  }

  if (!approved) {
    log_message(log, "system.package", "User declined installation of '" + package_name + "'.");
    return false;
  }

  log_message(log, "system.package", "Installing '" + package_name + "' via " + pm_name + ".");
  std::cout << "\nInstalling '" << package_name << "' via " << pm_name
            << "...\n" << install_cmd << "\n";

  int rc = run_command(install_cmd);
  if (rc != 0) {
    std::ostringstream oss;
    oss << "Installation command failed for '" << package_name << "' (exit code " << rc << ")";
    log_error(log, "system.package", oss.str());
    std::cerr << oss.str() << "\n";
    return false;
  }

  if (!is_package_installed(package_name)) {
    std::ostringstream oss;
    oss << "Installation did not make '" << package_name << "' available.";
    log_error(log, "system.package", oss.str());
    std::cerr << oss.str() << "\n";
    return false;
  }

  log_message(log, "system.package", "Package '" + package_name + "' installed successfully.");
  std::cout << "\nPackage '" << package_name << "' installed successfully.\n";
  return true;
}

} // namespace nazg::system

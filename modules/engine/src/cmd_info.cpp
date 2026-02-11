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

#include "engine/cmd_info.hpp"
#include "system/sysinfo.hpp"
#include "system/terminal.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace nazg::system::ansi;

namespace nazg::engine {

namespace {

enum class InfoStyle { Auto, Minimal, Json };

using nazg::system::system_info;

// Format helpers
std::string uptime_hms(double seconds) {
  long s = static_cast<long>(seconds);
  long d = s / 86400;
  s %= 86400;
  long h = s / 3600;
  s %= 3600;
  long m = s / 60;
  s %= 60;
  std::ostringstream os;
  if (d)
    os << d << "d ";
  os << h << "h " << m << "m " << s << "s";
  return os.str();
}

std::string mem_usage(const system_info& info) {
  long used = info.mem_total_kb - info.mem_available_kb;
  auto mb = [](long kB) { return kB / 1024; };
  std::ostringstream os;
  os << mb(used) << "MiB / " << mb(info.mem_total_kb) << "MiB";
  return os.str();
}

std::string disk_usage(const system_info& info) {
  auto GiB = [](unsigned long long b) {
    return b / (1024ull * 1024ull * 1024ull);
  };
  std::ostringstream os;
  os << GiB(info.disk_used_bytes) << "GiB / " << GiB(info.disk_total_bytes) << "GiB";
  return os.str();
}

// ASCII logos
std::vector<std::string> arch_logo() {
  return {"                /\\",
          "               /  \\",
          "              /\\   \\",
          "             /  \\   \\",
          "            / /\\ \\   \\",
          "           / /  \\ \\   \\",
          "          / / /\\ \\ \\   \\",
          "         / / /  \\ \\ \\   \\",
          "        /_/ /____\\ \\_\\   \\",
          "           \\__________\\",
          "             N A Z G"};
}

std::vector<std::string> ubuntu_logo() {
  return {"            _",       "         .-(_)-.",    "        /  ___  \\",
          "       /.-'   `-.\\", "       \\__.-'-.__/", "        /  .-.  \\",
          "        \\ (   ) /",  "         `-`-'-`",    "          U B U",
          "          N T U ",    "            NAZG"};
}

std::vector<std::string> fedora_logo() {
  return {"         ______",
          "        / ____/___  ____ ___  ____ ",
          "       / / __/ __ \\/ __ `__ \\/ __ \\",
          "      / /_/ / /_/ / / / / / / /_/ /",
          "      \\____/\\____/_/ /_/ /_/ .___/ ",
          "                         /_/   NAZG"};
}

const std::vector<std::string>& pick_logo(const std::string& distro) {
  static auto arch = arch_logo();
  static auto ub = ubuntu_logo();
  static auto fed = fedora_logo();
  if (distro == "arch")
    return arch;
  if (distro == "ubuntu" || distro == "debian")
    return ub;
  if (distro == "fedora")
    return fed;
  return arch;
}

void pad_right(std::string& s, size_t w) {
  if (s.size() < w)
    s.append(w - s.size(), ' ');
}

// Rendering functions
void print_auto(const system_info& info, bool color) {
  const auto& logo = pick_logo(info.distro_id);
  std::vector<std::pair<std::string, std::string>> kv = {
      {"OS", info.distro_id + (info.distro_version.empty() ? "" : " " + info.distro_version)},
      {"Kernel", info.kernel},
      {"Arch", info.arch},
      {"Host", info.user + "@" + info.hostname},
      {"Shell", info.shell},
      {"Threads", std::to_string(info.cpu_threads)},
      {"Memory", mem_usage(info)},
      {"Disk", disk_usage(info)},
      {"C++", info.cxx_version.empty() ? info.cxx_path : info.cxx_version},
      {"Python", info.python_version.empty() ? info.python_path : info.python_version},
      {"Git", info.git_version},
      {"GPU", info.gpu.value_or("")},
      {"Uptime", uptime_hms(info.uptime_seconds)},
      {"PkgMgr", info.pkg_manager}};

  int kw = 16;
  for (auto& [k, v] : kv) {
    (void)v;
    if ((int)k.size() > kw)
      kw = (int)k.size();
  }

  const char* K = color ? f_blue : "";
  const char* V = color ? f_white : "";
  const char* R = color ? reset : "";

  size_t rows = std::max(logo.size(), kv.size());
  for (size_t i = 0; i < rows; i++) {
    std::ostringstream line;
    if (i < logo.size())
      line << (color ? f_cyan : "") << logo[i] << R;
    else
      line << " ";
    line << "   ";
    if (i < kv.size()) {
      std::string key = kv[i].first + ":";
      pad_right(key, kw + 1);
      line << K << key << R << " " << V << kv[i].second << R;
    }
    std::cout << line.str() << "\n";
  }

  if (color) {
    std::cout << "\n";
    const char* blocks[8] = {f_black, f_red, f_green, f_yellow,
                             f_blue,  f_magenta, f_cyan,  f_white};
    for (int i = 0; i < 8; i++)
      std::cout << blocks[i] << "██" << reset << " ";
    std::cout << "\n";
  }
}

void print_minimal(const system_info& info) {
  auto out = [&](const std::string& k, const std::string& v) {
    std::cout << k << ": " << v << "\n";
  };
  out("OS", info.distro_id + (info.distro_version.empty() ? "" : " " + info.distro_version));
  out("Kernel", info.kernel);
  out("Arch", info.arch);
  out("User", info.user);
  out("Home", info.home);
  out("Shell", info.shell);
  out("Threads", std::to_string(info.cpu_threads));
  out("Memory", mem_usage(info));
  out("Disk", disk_usage(info));
  out("C++", info.cxx_version.empty() ? info.cxx_path : info.cxx_version);
  out("Python", info.python_version.empty() ? info.python_path : info.python_version);
  out("Git", info.git_version);
  if (info.gpu)
    out("GPU", *info.gpu);
  out("Uptime", uptime_hms(info.uptime_seconds));
  out("PkgMgr", info.pkg_manager);
}

void print_json(const system_info& info) {
  auto esc = [&](const std::string& s) {
    std::string o;
    for (char c : s) {
      if (c == '"' || c == '\\')
        o.push_back('\\');
      o.push_back(c);
    }
    return o;
  };
  std::cout << "{";
  std::cout << "\"distro\":\"" << esc(info.distro_id) << "\",";
  std::cout << "\"ver\":\"" << esc(info.distro_version) << "\",";
  std::cout << "\"kernel\":\"" << esc(info.kernel) << "\",";
  std::cout << "\"arch\":\"" << esc(info.arch) << "\",";
  std::cout << "\"user\":\"" << esc(info.user) << "\",";
  std::cout << "\"home\":\"" << esc(info.home) << "\",";
  std::cout << "\"shell\":\"" << esc(info.shell) << "\",";
  std::cout << "\"threads\":" << info.cpu_threads << ",";
  std::cout << "\"mem\":\"" << esc(mem_usage(info)) << "\",";
  std::cout << "\"disk\":\"" << esc(disk_usage(info)) << "\",";
  std::cout << "\"cpp\":\"" << esc(info.cxx_version.empty() ? info.cxx_path : info.cxx_version) << "\",";
  std::cout << "\"python\":\"" << esc(info.python_version.empty() ? info.python_path : info.python_version) << "\",";
  std::cout << "\"git\":\"" << esc(info.git_version) << "\",";
  std::cout << "\"gpu\":\"" << esc(info.gpu.value_or("")) << "\",";
  std::cout << "\"uptime\":\"" << esc(uptime_hms(info.uptime_seconds)) << "\",";
  std::cout << "\"pkg\":\"" << esc(info.pkg_manager) << "\"";
  std::cout << "}\n";
}

// Command handler
int cmd_info(const directive::command_context& ctx, const directive::context& ectx) {
  (void)ectx; // Not using context for now

  InfoStyle style = InfoStyle::Auto;
  bool color = system::is_tty();

  // Parse args: [--minimal|--json] [--no-color]
  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--minimal") {
      style = InfoStyle::Minimal;
    } else if (arg == "--json") {
      style = InfoStyle::Json;
    } else if (arg == "--no-color") {
      color = false;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg info [options]\n"
                << "Options:\n"
                << "  --minimal     Plain key:value output\n"
                << "  --json        JSON output\n"
                << "  --no-color    Disable colors\n"
                << "  -h, --help    Show this help\n";
      return 0;
    }
  }

  auto info = system::detect_system_info();

  switch (style) {
    case InfoStyle::Json:
      print_json(info);
      break;
    case InfoStyle::Minimal:
      print_minimal(info);
      break;
    case InfoStyle::Auto:
      print_auto(info, color);
      break;
  }

  return 0;
}

} // namespace

void register_info_command(directive::registry& reg) {
  directive::command_spec spec;
  spec.name = "info";
  spec.summary = "Display system information (distro, kernel, hardware, tools)";
  spec.options = {
    {"--minimal", "", "Plain key:value output", false, false, "", ""},
    {"--json", "", "JSON output", false, false, "", ""},
    {"--no-color", "", "Disable colors", false, false, "", ""},
  };
  spec.run = cmd_info;
  reg.add(spec);
}

} // namespace nazg::engine

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

#include "directive/agent_utils.hpp"

#include "prompt/prompt.hpp"
#include "system/process.hpp"

#include <algorithm>
#include <string.h>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <termios.h>
#include <unistd.h>
#ifdef __unix__
#include <grp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace nazg::directive {
namespace fs = std::filesystem;

namespace {

std::string read_password(const std::string &prompt_text) {
  std::cout << prompt_text << ": " << std::flush;

  termios oldt{};
  if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
    std::string fallback;
    std::getline(std::cin, fallback);
    return fallback;
  }

  termios newt = oldt;
  newt.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  std::string password;
  std::getline(std::cin, password);

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  std::cout << "\n";
  return password;
}

bool looks_like_docker_permission_issue(const std::string &text) {
  std::string lower;
  lower.reserve(text.size());
  for (char ch : text) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }

  return lower.find("permission denied") != std::string::npos ||
         lower.find("cannot connect to the docker daemon") != std::string::npos ||
         lower.find("docker daemon") != std::string::npos ||
         lower.find("is the docker daemon running") != std::string::npos;
}

::nazg::system::CommandResult run_docker_command(const std::string &cmd,
                                                 bool use_sudo,
                                                 const std::string &sudo_password) {
  using ::nazg::system::shell_quote;

  std::string final_cmd = cmd + " 2>&1";
  if (use_sudo) {
    final_cmd = std::string("sudo -S -p '' ") + final_cmd;
    final_cmd = std::string("echo ") + shell_quote(sudo_password) +
                " | " + final_cmd;
  }

  ::nazg::system::CommandResult result{};
  std::array<char, 256> buffer{};
  FILE *pipe = popen(final_cmd.c_str(), "r");
  if (!pipe) {
    result.exit_code = -1;
    return result;
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    result.output.append(buffer.data());
    std::cout << buffer.data();
    std::cout.flush();
  }

  int status = pclose(pipe);
  if (status == -1) {
    result.exit_code = -1;
  }
#ifdef __unix__
  else if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else {
    result.exit_code = status;
  }
#else
  else {
    result.exit_code = status;
  }
#endif

  if (!result.output.empty() && result.output.back() == '\n') {
    result.output.pop_back();
  }
  return result;
}

void zero_string(std::string &value) {
  if (!value.empty()) {
    explicit_bzero(&value[0], value.size());
  }
}

} // namespace

void print_prompt_panel(const std::string &title,
                        const std::vector<std::string> &lines) {
  if (lines.empty())
    return;

  std::string header = "nazg • " + title;

  std::size_t term_width = 80;
#ifdef __unix__
  struct winsize ws {
  };
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    term_width = static_cast<std::size_t>(ws.ws_col);
#endif

  std::size_t max_len = header.size();
  for (const auto &line : lines) {
    max_len = std::max(max_len, line.size());
  }

  std::size_t wrap_width = max_len;
  if (term_width > 4) {
    wrap_width = std::min(wrap_width, term_width - 4);
  }
  if (wrap_width == 0)
    wrap_width = header.empty() ? static_cast<std::size_t>(1) : header.size();

  std::vector<std::string> wrapped_lines;
  wrapped_lines.reserve(lines.size());
  auto wrap_line = [&](const std::string &text) {
    if (text.empty()) {
      wrapped_lines.emplace_back();
      return;
    }
    std::size_t start = 0;
    while (start < text.size()) {
      std::size_t len = std::min(wrap_width, text.size() - start);
      wrapped_lines.emplace_back(text.substr(start, len));
      start += len;
    }
  };

  for (const auto &line : lines) {
    wrap_line(line);
  }

  std::size_t content_width = header.size();
  for (const auto &line : wrapped_lines) {
    content_width = std::max(content_width, line.size());
  }
  std::size_t width = content_width + 2; // padding for leading space after the box glyph

  auto repeat_utf8 = [](const char *glyph, std::size_t count) {
    std::string out;
    std::size_t len = std::char_traits<char>::length(glyph);
    out.reserve(count * len);
    for (std::size_t i = 0; i < count; ++i) {
      out.append(glyph, len);
    }
    return out;
  };

  auto print_row = [&](const char *prefix, const std::string &content) {
    std::cout << prefix;
    std::cout << ' ' << content;
    std::size_t remaining = width > content.size() + 1 ? width - content.size() - 1 : 0;
    std::cout << std::string(remaining, ' ') << '\n';
  };

  std::cout << '\n';
  std::cout << u8"┌" << repeat_utf8(u8"─", width) << '\n';
  print_row(u8"│", header);
  std::cout << u8"├" << repeat_utf8(u8"─", width) << '\n';
  if (wrapped_lines.empty()) {
    print_row(u8"│", "");
  } else {
    for (const auto &line : wrapped_lines) {
      print_row(u8"│", line);
    }
  }
  std::cout << u8"└" << repeat_utf8(u8"─", width) << '\n';
}

std::pair<std::string, std::string> current_user_and_group() {
  std::string user;
  std::string group;
#ifdef __unix__
  if (struct passwd *pw = getpwuid(getuid()))
    user = pw->pw_name ? pw->pw_name : "";
  if (struct group *gr = getgrgid(getgid()))
    group = gr->gr_name ? gr->gr_name : "";
#endif

  if (user.empty()) {
    if (const char *env_user = std::getenv("USER"))
      user = env_user;
    else if (const char *env_user = std::getenv("USERNAME"))
      user = env_user;
  }
  if (group.empty()) {
    if (const char *env_group = std::getenv("GROUP"))
      group = env_group;
  }

  if (group.empty())
    group = user;
  return {user, group};
}

bool ensure_tarball_ownership(const fs::path &tarball,
                              bool use_sudo,
                              std::string &error_output,
                              const directive::context &/*ectx*/) {
  if (!use_sudo)
    return true;

  auto [user, group] = current_user_and_group();
  if (user.empty())
    return true;

  std::string user_spec = group.empty() ? user : user + ':' + group;
  std::string cmd = std::string("sudo chown ") +
                    ::nazg::system::shell_quote(user_spec) + " " +
                    ::nazg::system::shell_quote(tarball.string());
  print_prompt_panel("agent package",
                     {"→ Adjusting exported file ownership", "cmd: " + cmd});

  auto res = ::nazg::system::run_command_capture(cmd + " 2>&1");
  if (res.exit_code != 0) {
    error_output = res.output.empty() ? "chown failed" : res.output;
    return false;
  }
  return true;
}

std::string random_token(std::size_t length) {
  static const char alphabet[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

  std::random_device rd;
  std::uniform_int_distribution<int> dist(
      0, static_cast<int>(sizeof(alphabet) - 2));

  std::string out;
  out.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    out.push_back(alphabet[dist(rd)]);
  }
  return out;
}

fs::path default_agent_tarball_path() {
  fs::path base;
  if (const char *xdg_state = std::getenv("XDG_STATE_HOME")) {
    if (*xdg_state)
      base = fs::path(xdg_state);
  }
  if (base.empty()) {
    if (const char *home = std::getenv("HOME")) {
      base = fs::path(home) / ".local" / "state";
    } else {
      base = fs::temp_directory_path();
    }
  }
  return fs::absolute(base / "nazg" / "nazg-agent.tar");
}

bool build_agent_container_image(const directive::context &ectx,
                                 const std::string &base_image,
                                 std::string &out_tag,
                                 fs::path &out_tar,
                                 fs::path &out_temp_dir) {
  using ::nazg::system::shell_quote;

  auto attempt = [&](bool use_sudo, const std::string &sudo_password,
                     std::string &error_output, bool &permission_issue) -> bool {
    permission_issue = false;
    error_output.clear();

    auto docker = [&](const std::string &cmd, const std::string &description) {
      print_prompt_panel("agent package",
                         {std::string("→ ") + description,
                          std::string("cmd: ") + cmd});
      return run_docker_command(cmd, use_sudo, sudo_password);
    };

    auto info_res = docker("docker info", "Checking Docker daemon");
    if (info_res.exit_code != 0) {
      error_output = info_res.output;
      permission_issue = looks_like_docker_permission_issue(info_res.output);
      return false;
    }

    fs::path temp_dir = fs::temp_directory_path() /
                        fs::path("nazg-agent-container-" + random_token(6));
    std::error_code ec;
    fs::create_directories(temp_dir, ec);
    if (ec) {
      error_output = "Failed to create temp directory: " + temp_dir.string();
      return false;
    }

    fs::path dockerfile_path = temp_dir / "Dockerfile";
    std::ostringstream dockerfile;
    dockerfile << "FROM " << base_image << " AS build\n"
              << "RUN apt-get update && apt-get install -y build-essential cmake git pkg-config libsqlite3-dev libcurl4-openssl-dev && rm -rf /var/lib/apt/lists/*\n"
              << "WORKDIR /src\n"
              << "COPY . /src\n"
              << "RUN cmake -S . -B build-agent -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-agent --target nazg-agent -j\n"
              << "\nFROM " << base_image << "\n"
              << "RUN apt-get update && apt-get install -y --no-install-recommends docker.io ca-certificates curl && rm -rf /var/lib/apt/lists/*\n"
              << "COPY --from=build /src/build-agent/nazg-agent /usr/local/bin/nazg-agent\n"
              << "ENTRYPOINT [\"/usr/local/bin/nazg-agent\"]\n";

    {
      std::ofstream df(dockerfile_path);
      if (!df) {
        error_output = "Failed to write Dockerfile at " + dockerfile_path.string();
        fs::remove_all(temp_dir, ec);
        return false;
      }
      df << dockerfile.str();
    }

    static const std::string packaged_tag = "nazg-agent:bundle";
    std::string tag = packaged_tag;
    std::string build_cmd = std::string("docker build --pull --rm --network host -t ") +
                            shell_quote(tag) + " -f " +
                            shell_quote(dockerfile_path.string()) + " " +
                            shell_quote(fs::current_path().string());

    auto build_res = docker(build_cmd, "Building nazg-agent image");
    if (build_res.exit_code != 0) {
      error_output = build_res.output;
      permission_issue = looks_like_docker_permission_issue(build_res.output);
      docker("docker image rm -f " + shell_quote(tag), "Cleaning up intermediate image");
      fs::remove_all(temp_dir, ec);
      return false;
    }

    fs::path tarball = temp_dir / "nazg-agent-image.tar";
    std::string save_cmd = std::string("docker save -o ") +
                           shell_quote(tarball.string()) + " " + shell_quote(tag);
    auto save_res = docker(save_cmd, "Exporting image layer tarball");
    if (save_res.exit_code != 0) {
      error_output = save_res.output;
      docker("docker image rm -f " + shell_quote(tag), "Cleaning up intermediate image");
      fs::remove_all(temp_dir, ec);
      return false;
    }

    if (!ensure_tarball_ownership(tarball, use_sudo, error_output, ectx)) {
      docker("docker image rm -f " + shell_quote(tag), "Cleaning up intermediate image");
      fs::remove_all(temp_dir, ec);
      return false;
    }

    docker("docker image rm -f " + shell_quote(tag), "Cleaning up intermediate image");

    out_tag = tag;
    out_tar = tarball;
    out_temp_dir = temp_dir;
    return true;
  };

  std::string error_output;
  bool permission_issue = false;
  if (attempt(false, "", error_output, permission_issue)) {
    return true;
  }

  if (!permission_issue) {
    if (!error_output.empty())
      std::cerr << error_output << std::endl;
    std::cerr << "Failed to build agent container image." << std::endl;
    return false;
  }

  prompt::Prompt sudo_prompt(ectx.log);
  sudo_prompt.title("agent package")
             .question("Retry docker commands with sudo?")
             .warning("A sudo password will be requested locally.");
  if (!sudo_prompt.confirm(true)) {
    std::cerr << "Aborted." << std::endl;
    return false;
  }

  std::string sudo_password = read_password(
      "Enter sudo password for local docker build");
  if (sudo_password.empty()) {
    std::cerr << "Aborted: sudo password is required." << std::endl;
    return false;
  }

  std::string sudo_error;
  bool sudo_perm_issue = false;
  bool ok = attempt(true, sudo_password, sudo_error, sudo_perm_issue);
  zero_string(sudo_password);

  if (ok)
    return true;

  if (!sudo_error.empty())
    std::cerr << sudo_error << std::endl;
  std::cerr << "Failed to build agent container image." << std::endl;
  return false;
}

} // namespace nazg::directive

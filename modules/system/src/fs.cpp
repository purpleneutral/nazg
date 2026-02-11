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

#include "system/fs.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace nazg::system {

std::string trim(const std::string &s) {
  auto b = s.begin(), e = s.end();
  while (b != e && std::isspace(static_cast<unsigned char>(*b)))
    ++b;
  while (e != b && std::isspace(static_cast<unsigned char>(*(e - 1))))
    --e;
  return std::string(b, e);
}

std::string read_file_line(const std::string &path) {
  std::ifstream in(path);
  std::string s;
  if (in)
    std::getline(in, s);
  return s;
}

std::string bytes_gib(unsigned long long b) {
  unsigned long long GiB = 1024ull * 1024ull * 1024ull;
  std::ostringstream os;
  os << (b / GiB) << "GiB";
  return os.str();
}

std::string expand_tilde(const std::string &p) {
  if (!p.empty() && p[0] == '~') {
    const char *home = std::getenv("HOME");
    if (home)
      return std::string(home) + p.substr(1);
  }
  return p;
}

std::vector<std::string> wrap_text(const std::string &s, int width) {
  std::vector<std::string> out;
  if (width <= 8) {
    out.push_back(s);
    return out;
  }
  std::istringstream iss(s);
  std::string word, line;
  while (iss >> word) {
    if (line.empty()) {
      line = word;
    } else if ((int)(line.size() + 1 + word.size()) <= width) {
      line.push_back(' ');
      line += word;
    } else {
      out.push_back(line);
      line = word;
    }
  }
  if (!line.empty())
    out.push_back(line);
  if (out.empty())
    out.push_back(std::string{});
  return out;
}

} // namespace nazg::system

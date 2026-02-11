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
#include <string>
#include <vector>

namespace nazg::system {

// Trim leading and trailing whitespace
std::string trim(const std::string &s);

// Read first line from a file
std::string read_file_line(const std::string &path);

// Format bytes in human-readable form (GiB)
std::string bytes_gib(unsigned long long bytes);

// Expand ~ to HOME directory
std::string expand_tilde(const std::string &path);

// Text wrapping for terminal output
std::vector<std::string> wrap_text(const std::string &s, int width);

} // namespace nazg::system

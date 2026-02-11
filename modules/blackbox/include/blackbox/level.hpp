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

namespace nazg::blackbox {

enum class level { TRACE = 0, DEBUG, INFO, SUCCESS, WARN, ERROR, FATAL, OFF };

// Parse level from string (case-insensitive)
// Returns default_level if string doesn't match any level
level parse_level(const std::string &str, level default_level = level::INFO);

} // namespace nazg::blackbox

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

#include "nexus/connection.hpp"
#include <algorithm>

namespace nazg::nexus {

std::optional<std::string>
QueryResult::Row::get(const std::string &col) const {
  auto it = std::find(columns.begin(), columns.end(), col);
  if (it == columns.end())
    return std::nullopt;
  size_t idx = std::distance(columns.begin(), it);
  if (idx >= values.size())
    return std::nullopt;
  return values[idx];
}

std::optional<int64_t>
QueryResult::Row::get_int(const std::string &col) const {
  auto val = get(col);
  if (!val || val->empty())
    return std::nullopt;
  try {
    return std::stoll(*val);
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace nazg::nexus

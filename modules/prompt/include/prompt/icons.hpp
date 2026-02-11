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

namespace nazg::prompt {

enum class Icon {
  SUCCESS,
  ERROR,
  WARNING,
  INFO,
  PROGRESS,
  ARROW,
  BULLET,
  CHECKBOX,
  CHECKBOX_CHECKED
};

class IconSet {
public:
  explicit IconSet(bool use_unicode = true);

  std::string get(Icon icon) const;
  std::string success() const { return get(Icon::SUCCESS); }
  std::string error() const { return get(Icon::ERROR); }
  std::string warning() const { return get(Icon::WARNING); }
  std::string info() const { return get(Icon::INFO); }
  std::string progress() const { return get(Icon::PROGRESS); }
  std::string arrow() const { return get(Icon::ARROW); }
  std::string bullet() const { return get(Icon::BULLET); }

private:
  bool unicode_;
};

} // namespace nazg::prompt

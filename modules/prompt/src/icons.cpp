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

#include "prompt/icons.hpp"

namespace nazg::prompt {

IconSet::IconSet(bool use_unicode) : unicode_(use_unicode) {}

std::string IconSet::get(Icon icon) const {
  if (unicode_) {
    switch (icon) {
      case Icon::SUCCESS:           return "✓";
      case Icon::ERROR:             return "✗";
      case Icon::WARNING:           return "⚠";
      case Icon::INFO:              return "ℹ";
      case Icon::PROGRESS:          return "⏳";
      case Icon::ARROW:             return "→";
      case Icon::BULLET:            return "•";
      case Icon::CHECKBOX:          return "☐";
      case Icon::CHECKBOX_CHECKED:  return "☑";
    }
  } else {
    switch (icon) {
      case Icon::SUCCESS:           return "[OK]";
      case Icon::ERROR:             return "[FAIL]";
      case Icon::WARNING:           return "[WARN]";
      case Icon::INFO:              return "[INFO]";
      case Icon::PROGRESS:          return "[...]";
      case Icon::ARROW:             return ">";
      case Icon::BULLET:            return "*";
      case Icon::CHECKBOX:          return "[ ]";
      case Icon::CHECKBOX_CHECKED:  return "[x]";
    }
  }
  return "";
}

} // namespace nazg::prompt

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

#include "tui/theme.hpp"
#include <ftxui/screen/color.hpp>

namespace nazg::tui {

using namespace ftxui;

Theme cyberpunk_theme() {
  Theme theme;
  theme.name = "cyberpunk";

  // Cyberpunk purple/magenta/neon palette
  theme.active_border = Color::RGB(200, 0, 255);      // Bright purple/magenta
  theme.inactive_border = Color::RGB(80, 0, 100);     // Dim purple
  theme.pane_bg = Color::RGB(10, 0, 20);              // Deep purple-black
  theme.pane_fg = Color::RGB(220, 180, 255);          // Light purple-white

  // Status bar - neon cyan on dark purple
  theme.status_bg = Color::RGB(40, 0, 80);            // Dark purple
  theme.status_fg = Color::RGB(0, 255, 255);          // Neon cyan
  theme.status_accent = Color::RGB(255, 0, 200);      // Hot pink

  // UI elements
  theme.highlight = Color::RGB(255, 0, 255);          // Bright magenta
  theme.dimmed = Color::RGB(100, 80, 120);            // Muted purple

  return theme;
}

Theme default_theme() {
  Theme theme;
  theme.name = "default";

  // Gruvbox-inspired
  theme.active_border = Color::Cyan;
  theme.inactive_border = Color::GrayDark;
  theme.pane_bg = Color::Black;
  theme.pane_fg = Color::White;

  theme.status_bg = Color::Blue;
  theme.status_fg = Color::White;
  theme.status_accent = Color::Yellow;

  theme.highlight = Color::Green;
  theme.dimmed = Color::GrayDark;

  return theme;
}

Theme get_theme(const std::string& name) {
  if (name == "cyberpunk") {
    return cyberpunk_theme();
  }
  return default_theme();
}

} // namespace nazg::tui

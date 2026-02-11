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

#include <ftxui/screen/color.hpp>
#include <string>

namespace nazg::tui {

/**
 * @brief TUI color theme
 */
struct Theme {
  std::string name;

  // Pane colors
  ftxui::Color active_border;
  ftxui::Color inactive_border;
  ftxui::Color pane_bg;
  ftxui::Color pane_fg;

  // Status bar colors
  ftxui::Color status_bg;
  ftxui::Color status_fg;
  ftxui::Color status_accent;

  // UI elements
  ftxui::Color highlight;
  ftxui::Color dimmed;
};

/**
 * @brief Cyberpunk purple theme
 */
Theme cyberpunk_theme();

/**
 * @brief Default theme (gruvbox-inspired)
 */
Theme default_theme();

/**
 * @brief Get theme by name
 */
Theme get_theme(const std::string& name);

} // namespace nazg::tui

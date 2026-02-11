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

#include "tui/theme.hpp"
#include <ftxui/dom/elements.hpp>
#include <string>

namespace nazg::tui {

/**
 * @brief Status bar for TUI application
 *
 * Displays:
 * - Left: Prefix indicator, mode, window info
 * - Right: Process info, shortcuts
 */
class StatusBar {
public:
  StatusBar() = default;

  /**
   * @brief Set whether prefix key is active
   */
  void set_prefix_active(bool active) { prefix_active_ = active; }

  /**
   * @brief Set current mode
   */
  void set_mode(const std::string& mode) { mode_ = mode; }

  /**
   * @brief Set process info (PID, status)
   */
  void set_process_info(pid_t pid, bool alive);

  /**
   * @brief Set additional info text
   */
  void set_info(const std::string& info) { info_ = info; }

  /**
   * @brief Set prefix hint text (e.g., Ctrl-a)
   */
  void set_prefix_hint(const std::string& hint) { prefix_hint_ = hint; }

  /**
   * @brief Update summary for window state
   */
  void set_window_summary(int index, int total, bool zoomed, const std::string& title);

  /**
   * @brief Render the status bar
   * @param width Terminal width
   * @param theme Color theme
   */
  ftxui::Element render(int width, const Theme& theme) const;

private:
  bool prefix_active_ = false;
  std::string mode_ = "INSERT";
  std::string info_;
  pid_t pid_ = 0;
  bool process_alive_ = true;
  std::string prefix_hint_ = "Ctrl-b";
  std::string window_summary_;

  /**
   * @brief Build left side of status bar
   */
  std::string build_left() const;

  /**
   * @brief Build info section
   */
  std::string build_info() const;

  /**
   * @brief Build right side of status bar
   */
  std::string build_right() const;
};

} // namespace nazg::tui

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

#include <chrono>
#include <functional>
#include <string>

namespace nazg::tui {

/**
 * @brief TUI modes (vim-like)
 */
enum class Mode {
  NORMAL,   // Navigate, manipulate panes/windows
  INSERT,   // Pass all input to active pane (default)
  VISUAL,   // Select and copy text
  COMMAND,  // Command line (`:` prefix)
  PREFIX    // After prefix key (Ctrl-B)
};

/**
 * @brief ModeManager handles mode transitions and state
 *
 * This manager is responsible for:
 * - Tracking current mode
 * - Mode transitions and validation
 * - Prefix key timeout handling
 * - Mode change callbacks
 */
class ModeManager {
public:
  ModeManager();

  /**
   * @brief Get current mode
   */
  Mode current() const { return current_mode_; }

  /**
   * @brief Get previous mode (before current)
   */
  Mode previous() const { return previous_mode_; }

  /**
   * @brief Enter a new mode
   * @param mode Mode to enter
   * @return true if mode changed
   */
  bool enter(Mode mode);

  /**
   * @brief Exit current mode (returns to previous mode)
   */
  bool exit_to_previous();

  /**
   * @brief Check if prefix is currently active
   */
  bool is_prefix_active() const;

  /**
   * @brief Activate prefix mode
   * @param timeout_ms Timeout in milliseconds
   */
  void activate_prefix(int timeout_ms = 1000);

  /**
   * @brief Deactivate prefix mode
   */
  void deactivate_prefix();

  /**
   * @brief Check if prefix has timed out
   * @return true if prefix was active and has now timed out
   */
  bool check_prefix_timeout();

  /**
   * @brief Get mode name as string
   */
  static std::string mode_name(Mode mode);

  /**
   * @brief Register a callback for mode changes
   * @param callback Function called when mode changes (old_mode, new_mode)
   */
  void on_mode_change(std::function<void(Mode, Mode)> callback);

private:
  Mode current_mode_ = Mode::INSERT;  // Default to INSERT (passthrough)
  Mode previous_mode_ = Mode::INSERT;
  bool prefix_active_ = false;
  std::chrono::steady_clock::time_point prefix_time_;
  int prefix_timeout_ms_ = 1000;
  std::function<void(Mode, Mode)> mode_change_callback_;
};

} // namespace nazg::tui

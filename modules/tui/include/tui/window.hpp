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

#include "tui/managers/layout.hpp"
#include "tui/pane.hpp"
#include "tui/theme.hpp"
#include <optional>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::tui {

/**
 * @brief Window manages a collection of panes with layout
 *
 * Responsibilities:
 * - Create and destroy panes
 * - Track active pane
 * - Split panes in different directions
 * - Navigate between panes
 * - Render all panes with proper positioning
 */
class Window {
public:
  /**
   * @brief Create a window with initial pane
   * @param shell Shell command to run in initial pane
   */
  explicit Window(const std::string& shell,
                  nazg::blackbox::logger* log = nullptr);

  /**
   * @brief Get all panes in this window
   */
  const std::vector<std::shared_ptr<Pane>>& panes() const { return panes_; }

  /**
   * @brief Split the active pane
   * @param direction Split direction
   * @param shell Shell command for new pane (defaults to same as active)
   * @return Newly created pane, or nullptr on failure
   */
  Pane* split_active(SplitDirection direction, const std::string& shell = "");

  /**
   * @brief Close the active pane
   * @return true if pane was closed (false if it's the last pane)
   */
  bool close_active();

  /**
   * @brief Navigate to pane in direction
   * @param direction Navigation direction ('h', 'j', 'k', 'l')
   * @return true if navigation succeeded
   */
  bool navigate(char direction);

  /**
   * @brief Set active pane
   * @param pane Pane to make active
   */
  void set_active(Pane* pane);

  /**
   * @brief Send input to active pane
   */
  void send_input(const std::string& data);

  /**
   * @brief Update all panes (read output from terminals)
   */
  void update();

  /**
   * @brief Save current layout configuration
   */
  bool save_layout();

  /**
   * @brief Restore previously saved layout
   */
  bool restore_layout();

  /**
   * @brief Render the window
   * @param width Window width
   * @param height Window height
   * @param theme Color theme
   */
  ftxui::Element render(int width, int height, const Theme& theme);

  /**
   * @brief Check if any pane is alive
   */
  bool has_alive_panes() const;

  /**
   * @brief Get process info for the active pane, if any
   */
  bool active_process_info(pid_t& pid_out, bool& alive_out) const;

  /**
   * @brief Toggle zoom state for active pane
   */
  bool toggle_zoom();

  /**
   * @brief Check if window is zoomed
   */
  bool is_zoomed() const { return zoomed_; }

  /**
   * @brief Set display name for the window
   */
  void set_name(const std::string& name);

  /**
   * @brief Get window name
   */
  std::string name() const;

private:
  std::vector<std::shared_ptr<Pane>> panes_;
  Pane* active_pane_ = nullptr;
  Layout layout_;
  std::string default_shell_;
  std::string name_;
  bool zoomed_ = false;
  Pane* zoom_target_ = nullptr;
  mutable std::mutex mutex_;
  nazg::blackbox::logger* log_ = nullptr;

  /**
   * @brief Create a new pane with the specified shell
   */
  Pane* create_pane(const std::string& shell);

  /**
   * @brief Remove a pane (but don't destroy it yet)
   */
  void remove_pane(Pane* pane);

  /**
   * @brief Render a layout node recursively
   */
  ftxui::Element render_node(const Layout::Node* node,
                             int width,
                             int height,
                             const Theme& theme);

  /**
   * @brief Set active pane (requires lock held)
   */
  void set_active_locked(Pane* pane);

  void log_debug(const std::string& msg) const;
  void log_info(const std::string& msg) const;
  void log_warn(const std::string& msg) const;
  void log_error(const std::string& msg) const;

  struct SavedLayout {
    Layout::Snapshot snapshot;
    size_t active_leaf_index = 0;
  };

  std::optional<SavedLayout> saved_layout_;
};

} // namespace nazg::tui

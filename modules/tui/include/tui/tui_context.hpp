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

#include "tui/managers/command_manager.hpp"
#include "tui/managers/key_manager.hpp"
#include "tui/managers/mode_manager.hpp"
#include "tui/managers/menu_manager.hpp"
#include "tui/command_bar.hpp"
#include "tui/window.hpp"
#include <ftxui/component/screen_interactive.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::config {
class store;
}

namespace nazg::tui {

class StatusBar;
class Theme;

/**
 * @brief TUIContext is the central context for the TUI system
 *
 * This provides:
 * - Access to all managers (KeyManager, ModeManager, CommandManager, etc.)
 * - Window management
 * - Public API for external modules to extend TUI functionality
 * - Unified interface for TUI operations
 *
 * External modules (like git) can use this to:
 * - Register keybindings
 * - Register commands
 * - Access TUI state
 */
class TUIContext {
public:
  /**
   * @brief Create TUI context
   */
  TUIContext(nazg::blackbox::logger* log = nullptr,
             nazg::config::store* cfg = nullptr);

  // Manager access
  KeyManager& keys() { return key_manager_; }
  const KeyManager& keys() const { return key_manager_; }

  ModeManager& modes() { return mode_manager_; }
  const ModeManager& modes() const { return mode_manager_; }

  CommandManager& commands() { return command_manager_; }
  const CommandManager& commands() const { return command_manager_; }
  CommandBar& command_bar() { return command_bar_; }
  const CommandBar& command_bar() const { return command_bar_; }

  MenuManager& menus() { return menu_manager_; }
  const MenuManager& menus() const { return menu_manager_; }


  // Window management
  /**
   * @brief Get currently active window
   */
  Window* active_window();
  const Window* active_window() const;

  /**
   * @brief Get all windows
   */
  std::vector<Window*> all_windows();
  std::vector<const Window*> all_windows() const;

  /**
   * @brief Create a new window
   * @param shell Shell command to run
   * @return Pointer to new window, or nullptr on failure
   */
  Window* create_window(const std::string& shell = "");

  /**
   * @brief Close a window
   * @param window Window to close
   * @return true if closed successfully
   */
  bool close_window(Window* window);

  /**
   * @brief Close the currently active window
   */
  bool close_active_window();

  /**
   * @brief Focus a window by index
   */
  bool focus_window(int index);

  /**
   * @brief Navigate windows (next, previous, last)
   */
  bool focus_next_window();
  bool focus_previous_window();
  bool focus_last_window();

  /**
   * @brief Get window count
   */
  size_t window_count() const;

  /**
   * @brief Get active window index
   */
  int active_window_index() const;

  // Menu management
  /**
   * @brief Get currently active menu
   */
  Menu* active_menu();
  const Menu* active_menu() const;

  // Component focus management
  /**
   * @brief Get currently focused component
   */
  ComponentBase* focused_component();
  const ComponentBase* focused_component() const;

  /**
   * @brief Focus next component (Tab key - sequential)
   */
  bool focus_next_component();

  /**
   * @brief Focus previous component (Shift+Tab - sequential)
   */
  bool focus_previous_component();

  /**
   * @brief Focus component to the left (h key - spatial)
   */
  bool focus_component_left();

  /**
   * @brief Focus component to the right (l key - spatial)
   */
  bool focus_component_right();

  /**
   * @brief Focus component above (k key - spatial)
   */
  bool focus_component_up();

  /**
   * @brief Focus component below (j key - spatial)
   */
  bool focus_component_down();

  /**
   * @brief Check if INSERT mode is enabled for current context
   * @return true if active menu supports INSERT mode and mode is INSERT
   */
  bool insert_mode_enabled() const;

  // Screen access
  ftxui::ScreenInteractive& screen() { return *screen_; }

  // Status and info
  void set_status_message(const std::string& msg);
  std::string get_status_message() const;

  void set_quit_flag() { should_quit_ = true; }
  bool should_quit() const { return should_quit_; }

  // Configuration
  std::string get_default_shell() const;
  void set_default_shell(const std::string& shell) { default_shell_ = shell; }

  // Logging
  nazg::blackbox::logger* logger() { return log_; }
  void log_info(const std::string& msg) const;
  void log_debug(const std::string& msg) const;
  void log_warn(const std::string& msg) const;
  void log_error(const std::string& msg) const;

  /**
   * @brief Initialize context (called after construction)
   */
  void initialize();

private:
  friend class TUIApp;

  // External dependencies (declared first: used by manager constructors)
  nazg::blackbox::logger* log_ = nullptr;
  nazg::config::store* cfg_ = nullptr;

  // Managers
  KeyManager key_manager_;
  ModeManager mode_manager_;
  CommandManager command_manager_;
  MenuManager menu_manager_;
  CommandBar command_bar_;

  // Windows
  std::vector<std::unique_ptr<Window>> windows_;
  int active_window_index_ = 0;
  int last_window_index_ = 0;
  mutable std::mutex windows_mutex_;

  // State
  std::string default_shell_;
  std::string status_message_;
  bool should_quit_ = false;
  mutable std::mutex state_mutex_;

  // Component focus tracking
  ComponentBase* focused_component_ = nullptr;
  mutable std::mutex focus_mutex_;

  ftxui::ScreenInteractive* screen_ = nullptr;

  /**
   * @brief Set screen (called by TUIApp)
   */
  void set_screen(ftxui::ScreenInteractive* screen) { screen_ = screen; }
};

} // namespace nazg::tui

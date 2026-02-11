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

#include "tui/tui_context.hpp"
#include "tui/statusbar.hpp"
#include "tui/theme.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <memory>
#include <string>

namespace nazg::blackbox {
class logger;
}

namespace nazg::config {
class store;
}

namespace nazg::nexus {
class Store;
}

namespace nazg::tui {

/**
 * @brief Main TUI application
 *
 * This is the entry point for the TUI multiplexer using the manager-based architecture.
 *
 * The new architecture uses:
 * - TUIContext: Central context holding all managers and state
 * - KeyManager: Manages keybindings and provides API for external modules
 * - ModeManager: Handles mode transitions (NORMAL, INSERT, PREFIX, etc.)
 * - CommandManager: Command registry and execution
 * - Window/Pane: Terminal pane management
 */
class TUIApp {
public:
  /**
   * @brief Create TUI application
   * @param log Logger instance (optional)
   * @param cfg Config store (optional)
   */
  TUIApp(nazg::blackbox::logger* log = nullptr,
         nazg::config::store* cfg = nullptr);

  /**
   * @brief Register built-in menus (docker, etc.)
   * @param store Nexus store for database access
   */
  void register_menus(nazg::nexus::Store* store);

  /**
   * @brief Run the TUI application (blocks until exit)
   * @return Exit code
   */
  int run();

  /**
   * @brief Get the TUI context (for external module access)
   */
  TUIContext& context() { return *ctx_; }
  const TUIContext& context() const { return *ctx_; }

private:
  std::unique_ptr<TUIContext> ctx_;
  ftxui::ScreenInteractive screen_;
  Theme theme_;
  StatusBar statusbar_;
  bool saw_escape_ = false;

  /**
   * @brief Create the main component
   */
  ftxui::Component make_component();

  /**
   * @brief Handle keyboard input
   */
  bool handle_input(ftxui::Event event);

  /**
   * @brief Handle input when prefix is active
   */
  bool handle_prefix_input(ftxui::Event event);

  /**
   * @brief Handle input in INSERT mode (pass to terminal)
   */
  bool handle_insert_input(ftxui::Event event);

  /**
   * @brief Render the UI
   */
  ftxui::Element render();

  /**
   * @brief Update loop - read from terminal
   */
  void update_loop();

  /**
   * @brief Update status bar with current state
   */
  void update_status_bar();
};

} // namespace nazg::tui

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

#include "tui/component_base.hpp"
#include <map>
#include <memory>
#include <string>

namespace nazg::tui {

// Forward declaration
class TUIContext;

/**
 * @brief Base class for all menus
 *
 * A Menu is a top-level component tree that represents a complete screen/view.
 * Menus are registered with MenuManager and can be loaded dynamically via
 * the :load command.
 *
 * Usage:
 *   1. Inherit from Menu
 *   2. Override build() to construct component tree
 *   3. Register with MenuManager
 *   4. Load via :load <menu-id>
 *
 * Example:
 * ```cpp
 * class MainMenu : public Menu {
 * public:
 *   std::string id() const override { return "MainMenu"; }
 *   std::string title() const override { return "Main Menu"; }
 *
 *   void build(TUIContext& ctx) override {
 *     set_root(
 *       Box::vertical()
 *         .border(true)
 *         .title("Nazg")
 *         .children({
 *           MenuList::create()
 *             .items({"Git", "Tasks", "Quit"})
 *             .on_select([&](int i) { handle_selection(ctx, i); })
 *         })
 *     );
 *   }
 * };
 * ```
 */
class Menu {
public:
  virtual ~Menu() = default;

  // ============================================================================
  // Identity
  // ============================================================================

  /**
   * @brief Get unique menu identifier (used for :load command)
   */
  virtual std::string id() const = 0;

  /**
   * @brief Get human-readable menu title
   */
  virtual std::string title() const = 0;

  // ============================================================================
  // Mode Support
  // ============================================================================

  /**
   * @brief Check if menu supports INSERT mode
   *
   * If true, user can press 'i' on a focused component to enter INSERT mode.
   * INSERT mode allows text input components to receive all key events.
   * ESC exits INSERT mode and returns to NORMAL mode.
   *
   * @return true if menu has components that need INSERT mode
   */
  virtual bool supports_insert_mode() const { return false; }

  /**
   * @brief Check if menu supports VISUAL mode
   *
   * If true, user can press 'v' to enter VISUAL mode for text selection.
   *
   * @return true if menu has components that support visual selection
   */
  virtual bool supports_visual_mode() const { return false; }

  // ============================================================================
  // State Preservation
  // ============================================================================

  /**
   * @brief Menu state for preservation across navigation
   */
  struct MenuState {
    std::map<std::string, std::string> data;  // Key-value state storage
  };

  /**
   * @brief Check if menu state should be preserved when navigating away
   *
   * If true (default), component state (selection, scroll position, input
   * content) is saved when the user navigates to another menu and restored
   * when they return via :back.
   *
   * Override to return false for menus that should always reset.
   *
   * @return true to preserve state, false to reset on each load
   */
  virtual bool preserve_state() const { return true; }

  /**
   * @brief Save current menu state
   *
   * Called when navigating away from this menu (if preserve_state() is true).
   * Override to save custom state data.
   *
   * @return State to preserve
   */
  virtual MenuState save_state() const { return MenuState{}; }

  /**
   * @brief Restore previously saved state
   *
   * Called when returning to this menu via :back (if preserve_state() is true).
   * Override to restore custom state data.
   *
   * @param state Previously saved state
   */
  virtual void restore_state(const MenuState& /*state*/) {}

  // ============================================================================
  // Component Tree Construction
  // ============================================================================

  /**
   * @brief Build the component tree
   *
   * Called once when the menu is loaded. Use set_root() to provide the
   * root component of your menu's component tree.
   *
   * @param ctx TUI context for accessing managers and registering callbacks
   */
  virtual void build(TUIContext& ctx) = 0;

  /**
   * @brief Get the root component of this menu
   */
  ComponentBase* root() const { return root_.get(); }

  // ============================================================================
  // Lifecycle Hooks
  // ============================================================================

  /**
   * @brief Called when menu is pushed onto the navigation stack
   *
   * This is called when the menu first loads via :load.
   * Use for initialization, data loading, etc.
   */
  virtual void on_load() {}

  /**
   * @brief Called when menu is popped from the navigation stack
   *
   * This is called when the user navigates away via :load or :back.
   * Use for cleanup, saving data, etc.
   */
  virtual void on_unload() {}

  /**
   * @brief Called when menu becomes active again after :back
   *
   * This is called when the user returns to this menu from a deeper menu.
   * Use to refresh data that might have changed.
   */
  virtual void on_resume() {}

protected:
  /**
   * @brief Set the root component (called from build())
   * @param root Root component (ownership transferred)
   */
  void set_root(std::unique_ptr<ComponentBase> root) {
    root_ = std::move(root);
  }

private:
  std::unique_ptr<ComponentBase> root_;
};

} // namespace nazg::tui

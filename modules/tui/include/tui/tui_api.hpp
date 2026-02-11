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

/**
 * @file tui_api.hpp
 * @brief Public API for extending the TUI module
 *
 * This header provides the public API that external modules (like git, task, etc.)
 * can use to extend TUI functionality by registering keybindings and commands.
 *
 * Example usage from a module:
 *
 * ```cpp
 * #include "tui/tui_api.hpp"
 *
 * // In your module's initialization:
 * void init_tui_integration(nazg::tui::TUIContext& tui) {
 *   using namespace nazg::tui;
 *
 *   // Register a custom command
 *   tui.commands().register_command(
 *     "git-status",
 *     "Show git status in TUI",
 *     [](TUIContext& ctx, const std::vector<std::string>& args) {
 *       // Your implementation
 *       ctx.set_status_message("Running git status...");
 *       // ... run git status logic ...
 *       return true;
 *     }
 *   );
 *
 *   // Register a keybinding for your command
 *   KeyBinding kb("g", "git-status", "Show git status", Mode::PREFIX, true);
 *   tui.keys().bind(kb);
 * }
 * ```
 *
 * Available APIs:
 *
 * 1. **KeyManager** (`tui.keys()`):
 *    - `bind(KeyBinding)` - Register a keybinding
 *    - `unbind(key, mode, is_prefix)` - Remove a keybinding
 *    - `get_bindings(mode, is_prefix)` - Query bindings
 *
 * 2. **CommandManager** (`tui.commands()`):
 *    - `register_command(name, description, func)` - Add a command
 *    - `unregister_command(name)` - Remove a command
 *    - `execute(ctx, name, args)` - Execute a command
 *
 * 3. **ModeManager** (`tui.modes()`):
 *    - `current()` - Get current mode
 *    - `enter(mode)` - Switch modes
 *    - `is_prefix_active()` - Check if prefix is active
 *
 * 4. **Window Management**:
 *    - `tui.active_window()` - Get active window
 *    - `tui.create_window(shell)` - Create new window
 *    - `tui.focus_*_window()` - Navigate windows
 *
 * 5. **Status and Logging**:
 *    - `tui.set_status_message(msg)` - Update status bar
 *    - `tui.log_info(msg)` - Log message
 */

#include "tui/tui_context.hpp"
#include "tui/managers/key_manager.hpp"
#include "tui/managers/command_manager.hpp"
#include "tui/managers/mode_manager.hpp"
#include "tui/window.hpp"
#include "tui/pane.hpp"

namespace nazg::tui {

/**
 * @brief Register a module's TUI integration
 *
 * Call this from your module's initialization to add custom
 * keybindings and commands to the TUI.
 *
 * @param tui TUIContext instance
 * @param module_name Name of your module (for logging)
 * @param init_func Function to initialize your TUI integration
 *
 * Example:
 * ```cpp
 * register_tui_module(tui, "git", [](TUIContext& ctx) {
 *   ctx.commands().register_command("git-status", "Git status", ...);
 *   ctx.keys().bind({"g", "git-status", "Git status"});
 * });
 * ```
 */
inline void register_tui_module(
    TUIContext& tui,
    const std::string& module_name,
    std::function<void(TUIContext&)> init_func) {
  tui.log_info("Registering TUI integration for module: " + module_name);
  try {
    init_func(tui);
    tui.log_info("TUI integration registered for module: " + module_name);
  } catch (const std::exception& e) {
    tui.log_error("Failed to register TUI integration for " + module_name +
                  ": " + e.what());
  }
}

} // namespace nazg::tui

/**
 * @example git_tui_integration.cpp
 * Example showing how the git module could integrate with TUI:
 *
 * ```cpp
 * #include "tui/tui_api.hpp"
 * #include "git/git.hpp"
 *
 * namespace nazg::git {
 *
 * void init_tui_integration(tui::TUIContext& tui) {
 *   using namespace nazg::tui;
 *
 *   // Register git commands
 *   tui.commands().register_command(
 *     "git-status",
 *     "Show git repository status",
 *     [](TUIContext& ctx, const std::vector<std::string>& args) {
 *       auto status = git::get_status();
 *       ctx.set_status_message("Git: " + status.summary());
 *       ctx.log_info("=== Git Status ===\n" + status.detailed());
 *       return true;
 *     }
 *   );
 *
 *   tui.commands().register_command(
 *     "git-commit",
 *     "Create a git commit",
 *     [](TUIContext& ctx, const std::vector<std::string>& args) {
 *       if (args.empty()) {
 *         ctx.set_status_message("Usage: git-commit <message>");
 *         return false;
 *       }
 *       std::string message = args[0];
 *       bool success = git::commit(message);
 *       ctx.set_status_message(success ? "Committed" : "Commit failed");
 *       return success;
 *     }
 *   );
 *
 *   // Register keybindings (prefix + g for git commands)
 *   tui.keys().bind({"g", "git-status", "Git status", Mode::PREFIX, true});
 *   tui.keys().bind({"G", "git-commit", "Git commit", Mode::PREFIX, true});
 *
 *   ctx.log_info("Git TUI integration complete");
 * }
 *
 * } // namespace nazg::git
 * ```
 */

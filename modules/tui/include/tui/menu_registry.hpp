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

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::tui {

class TUIContext;

/**
 * @brief Register all built-in menus with the TUI context
 *
 * This should be called during TUI initialization to make built-in menus
 * available via the :load command.
 *
 * @param ctx TUI context to register menus with
 * @param store Nexus store for database access
 * @param log Logger instance (optional)
 */
void register_builtin_menus(TUIContext& ctx,
                           nazg::nexus::Store* store,
                           nazg::blackbox::logger* log = nullptr);

} // namespace nazg::tui

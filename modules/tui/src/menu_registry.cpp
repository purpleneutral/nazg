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

#include "tui/menu_registry.hpp"
#include "tui/menus/docker_menu.hpp"
#include "tui/menus/main_menu.hpp"
#include "tui/tui_context.hpp"
#include "nexus/store.hpp"
#include "blackbox/logger.hpp"

namespace nazg::tui {

void register_builtin_menus(TUIContext& ctx,
                           nazg::nexus::Store* store,
                           nazg::blackbox::logger* log) {
  if (!store) {
    if (log) {
      log->warn("menu_registry", "Cannot register menus: store is null");
    }
    return;
  }

  // Register Main menu
  ctx.menus().register_menu("main", [log]() {
    return std::make_unique<MainMenu>(log);
  });

  // Register Docker menu
  ctx.menus().register_menu("docker", [store, log]() {
    return std::make_unique<DockerMenu>(store, nullptr, log);
  });

  if (log) {
    log->info("menu_registry", "Registered built-in menus: main, docker");
  }
}

} // namespace nazg::tui

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

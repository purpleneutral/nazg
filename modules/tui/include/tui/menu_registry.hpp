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

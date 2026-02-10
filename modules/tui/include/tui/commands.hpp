#pragma once

namespace nazg::directive {
class registry;
struct context;
} // namespace nazg::directive

namespace nazg::tui {

/**
 * @brief Register TUI commands with the directive system
 */
void register_commands(directive::registry& reg, const directive::context& ctx);

} // namespace nazg::tui

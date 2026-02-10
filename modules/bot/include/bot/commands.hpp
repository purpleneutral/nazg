#pragma once

namespace nazg::directive {
struct command_context;
struct context;
class registry;
} // namespace nazg::directive

namespace nazg::bot {

// Register all bot commands with the directive registry
void register_commands(directive::registry &reg, const directive::context &ctx);

} // namespace nazg::bot

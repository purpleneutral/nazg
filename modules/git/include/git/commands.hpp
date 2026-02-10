#pragma once

namespace nazg::directive {
struct command_context;
struct context;
class registry;
} // namespace nazg::directive

namespace nazg::git {

// Register all git commands with the directive registry
void register_commands(directive::registry &reg, const directive::context &ctx);

} // namespace nazg::git

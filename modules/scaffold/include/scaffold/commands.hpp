#pragma once

namespace nazg::directive {
struct context;
class registry;
} // namespace nazg::directive

namespace nazg::scaffold {

// Register all scaffold commands with the directive registry
void register_commands(directive::registry &reg, const directive::context &ctx);

} // namespace nazg::scaffold

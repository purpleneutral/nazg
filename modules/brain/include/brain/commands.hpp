#pragma once

namespace nazg::directive {
struct context;
class registry;
} // namespace nazg::directive

namespace nazg::brain {

// Register all brain/build commands with the directive registry
void register_commands(directive::registry &reg, const directive::context &ctx);

} // namespace nazg::brain

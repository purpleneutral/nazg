#pragma once

namespace nazg::directive {
class registry;
struct context;
}  // namespace nazg::directive

namespace nazg::test {

// Register test module commands
void register_commands(nazg::directive::registry &reg,
                       nazg::directive::context &ctx);

}  // namespace nazg::test

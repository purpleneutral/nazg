#pragma once

namespace nazg::directive {
struct context;
class registry;

void register_db_commands(registry &reg, const context &ctx);

} // namespace nazg::directive


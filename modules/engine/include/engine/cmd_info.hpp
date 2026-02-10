#pragma once

namespace nazg::directive { class registry; }

namespace nazg::engine {

// Register the system info command
void register_info_command(::nazg::directive::registry& reg);

} // namespace nazg::engine

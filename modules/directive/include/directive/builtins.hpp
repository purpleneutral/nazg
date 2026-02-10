#pragma once
#include "registry.hpp"

namespace nazg::directive {

// Register the built-in `info` command (and optionally others later)
void register_info(registry& r);

} // namespace nazg::directive

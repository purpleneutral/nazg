// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 purpleneutral
//
// This file is part of nazg.
//
// nazg is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// nazg is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along
// with nazg. If not, see <https://www.gnu.org/licenses/>.

#include "blackbox/logger.hpp"
#include "blackbox/options.hpp"
#include "engine/runtime.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char **argv) {

  nazg::engine::options rt{};
  nazg::engine::runtime engine{rt};
  engine.init_logging();

  if (auto *log = engine.logger()) {
    log->info("App", "nazg starting");

    std::ostringstream args;
    args << "argv[0]=" << (argc > 0 ? argv[0] : "<none>");
    for (int i = 1; i < argc; ++i) {
      args << ", argv[" << i << "]=" << argv[i];
    }
    log->debug("App", args.str());
  }

  engine.init_nexus();
  engine.init_commands();

  // Log startup (will only show if console logging is enabled)
  if (auto *log = engine.logger()) {
    log->debug("Engine", "Engine initialization complete");
    log->debug("Engine",
               "console_colors=" + std::to_string(rt.log.console_colors));
  }

  int result = engine.dispatch(argc, argv);

  if (auto *log = engine.logger()) {
    log->info("App", "nazg exiting with code " + std::to_string(result));
  }

  return result;
}

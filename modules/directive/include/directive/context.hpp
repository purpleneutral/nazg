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

#pragma once
#include <string>
#include <vector>

namespace nazg {
namespace blackbox {
class logger;
}
namespace nexus {
class Store;
}
namespace config {
class store;
}
// namespace palantir {
// struct args;
// } // namespace palantir
} // namespace nazg

namespace nazg::directive {

class registry;

struct context {
  int argc = 0;
  char **argv = nullptr;

  // parsed top-level args (palantír)
  // const ::nazg::palantir::args *parsed = nullptr;

  // logger (Westmarch)
  ::nazg::blackbox::logger *log = nullptr;

  // database (Nexus)
  ::nazg::nexus::Store *store = nullptr;

  // configuration
  ::nazg::config::store *cfg = nullptr;

  // handy: program name and raw trailing tokens (after command)
  std::string prog;
  std::vector<std::string> positionals;

  bool verbose = false;
  const registry *reg = nullptr;
};

} // namespace nazg::directive

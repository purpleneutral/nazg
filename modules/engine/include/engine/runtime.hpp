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
#include <memory>
#include <string>
#include <vector>

#include "blackbox/logger.hpp"
#include "blackbox/options.hpp"

namespace nazg::blackbox {
class logger;
}
namespace nazg::directive {
class registry;
struct context;
} // namespace nazg::directive
namespace nazg::config {
class store;
}
namespace nazg::nexus {
class Store;
}

namespace nazg::engine {

struct options {
  ::nazg::blackbox::options log;
  bool verbose = false;
  std::string extra_plugin_path;
};

class runtime {
public:
  explicit runtime(const options &opts);
  ~runtime();

  // boots logging (blackbox)
  void init_logging();
  ::nazg::blackbox::logger *logger() const;

  // Commands (Directive)
  void init_commands();
  directive::registry &registry();
  int dispatch(int argc, char **argv);

  // Config access
  const config::store *config() const;

  // Database (Nexus)
  void init_nexus();
  nexus::Store *nexus() const;

private:
  struct impl;
  std::unique_ptr<impl> p_;
};

} // namespace nazg::engine

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

#include "git/server.hpp"
#include "git/cgit.hpp"
#include "git/gitea.hpp"
#include <stdexcept>

namespace nazg::git {

std::unique_ptr<Server> create_server(
    const ServerConfig& cfg,
    nazg::nexus::Store* store,
    nazg::blackbox::logger* log) {

  if (cfg.type == "cgit") {
    return std::make_unique<CgitServer>(cfg, store, log);
  } else if (cfg.type == "gitea") {
    return std::make_unique<GiteaServer>(cfg, store, log);
  }
  // Future: gitlab, etc.

  throw std::runtime_error("Unknown server type: " + cfg.type);
}

} // namespace nazg::git

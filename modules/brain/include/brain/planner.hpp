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
#include "brain/types.hpp"
#include <string>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::brain {

struct ProjectInfo;
struct SnapshotResult;

// Decide what action to take
class Planner {
public:
  explicit Planner(nazg::nexus::Store *store, nazg::blackbox::logger *log = nullptr);

  // Decide action based on project info and snapshot
  Plan decide(int64_t project_id, const ProjectInfo &info, const SnapshotResult &snapshot);

  // Generate test plan for detected test framework
  Plan generate_test_plan(const ProjectInfo &info);

private:
  // Generate build command for detected build system
  Plan generate_build_plan(const ProjectInfo &info);

  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;
};

} // namespace nazg::brain

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
#include "task/executor.hpp"
#include <string>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::brain {
struct Plan;
}

namespace nazg::task {

// Build orchestration
class Builder {
public:
  explicit Builder(nazg::nexus::Store *store, nazg::blackbox::logger *log = nullptr);

  // Execute a build plan
  ExecutionResult build(int64_t project_id, const nazg::brain::Plan &plan);

  // Record build result to database
  void record_build(int64_t project_id, const nazg::brain::Plan &plan,
                    const ExecutionResult &result);

private:
  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;
  Executor executor_;
};

} // namespace nazg::task

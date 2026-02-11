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

namespace nazg {
namespace blackbox {
class logger;
}
} // namespace nazg

namespace nazg::test::workspace_suite {

bool run_snapshot_creation(nazg::blackbox::logger *log, std::string &error);
bool run_prune_behavior(nazg::blackbox::logger *log, std::string &error);
bool run_env_capture(nazg::blackbox::logger *log, std::string &error);
bool run_restore_full(nazg::blackbox::logger *log, std::string &error);

} // namespace nazg::test::workspace_suite


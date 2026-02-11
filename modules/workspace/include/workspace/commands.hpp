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

namespace nazg::directive {
class registry;
struct context;
} // namespace nazg::directive

namespace nazg::workspace {

// Register all workspace commands
void register_commands(nazg::directive::registry &reg,
                       nazg::directive::context &ctx);

// Command handlers
int cmd_workspace_snapshot(const std::vector<std::string> &args,
                           nazg::directive::context &ctx);
int cmd_workspace_history(const std::vector<std::string> &args,
                          nazg::directive::context &ctx);
int cmd_workspace_show(const std::vector<std::string> &args,
                       nazg::directive::context &ctx);
int cmd_workspace_diff(const std::vector<std::string> &args,
                       nazg::directive::context &ctx);
int cmd_workspace_restore(const std::vector<std::string> &args,
                          nazg::directive::context &ctx);
int cmd_workspace_tag(const std::vector<std::string> &args,
                      nazg::directive::context &ctx);
int cmd_workspace_prune(const std::vector<std::string> &args,
                        nazg::directive::context &ctx);

} // namespace nazg::workspace

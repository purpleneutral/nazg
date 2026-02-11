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

#include "directive/db_commands.hpp"

#include "directive/context.hpp"
#include "directive/registry.hpp"

#include "nexus/config.hpp"
#include "nexus/store.hpp"
#include "prompt/prompt.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace nazg::directive {
namespace {

void print_db_help() {
  std::cout << "Usage: nazg db <command> [options]\n"
            << "Commands:\n"
            << "  reset [--force]     Reset the Nexus database (drops all data).\n";
}

int cmd_db_reset(const command_context &ctx, const context &ectx) {
  if (!ectx.store) {
    std::cerr << "Error: database not initialised." << std::endl;
    return 1;
  }

  bool force = false;
  for (int i = 3; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--force" || arg == "--yes" || arg == "-y") {
      force = true;
    } else {
      std::cerr << "Unknown option for db reset: " << arg << "\n";
      print_db_help();
      return 1;
    }
  }

  std::string db_path = "(unknown)";
  if (ectx.cfg) {
    auto nexus_cfg = ::nazg::nexus::Config::from_config(*ectx.cfg);
    db_path = nexus_cfg.db_path;
  }

  if (!force) {
    prompt::Prompt confirm_prompt(ectx.log);
    confirm_prompt.title("db reset")
                  .question("This will erase all Nexus data at " + db_path)
                  .warning("All tracked servers, history, and agent state will be removed.")
                  .info("Pass --force to skip this confirmation.");
    if (!confirm_prompt.confirm(false)) {
      std::cout << "Aborted." << std::endl;
      return 1;
    }
  }

  if (!ectx.store->reset_database()) {
    std::cerr << "Failed to reset database. Check logs for details." << std::endl;
    return 1;
  }

  std::cout << "✓ Nexus database reset";
  if (!db_path.empty())
    std::cout << " (" << db_path << ")";
  std::cout << "\n";
  return 0;
}

int cmd_db(const command_context &ctx, const context &ectx) {
  if (ctx.argc < 3) {
    print_db_help();
    return 1;
  }

  std::string sub = ctx.argv[2];
  if (sub == "reset")
    return cmd_db_reset(ctx, ectx);

  std::cerr << "Unknown db subcommand: " << sub << "\n";
  print_db_help();
  return 1;
}

} // namespace

void register_db_commands(registry &reg, const context &ctx) {
  (void)ctx;
  command_spec db_spec{};
  db_spec.name = "db";
  db_spec.summary = "Manage the Nexus persistence database";
  db_spec.run = &cmd_db;
  reg.add(db_spec);
}

} // namespace nazg::directive


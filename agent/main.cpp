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

#include "agent/runtime.hpp"
#include "blackbox/logger.hpp"
#include "blackbox/options.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace {
std::atomic<bool> g_shutdown{false};

void signal_handler(int /*sig*/) {
  g_shutdown.store(true);
}
} // namespace

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  // Install signal handlers for graceful shutdown
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  ::nazg::blackbox::options log_opts;
  log_opts.console_enabled = true;
  auto logger = std::make_unique<::nazg::blackbox::logger>(log_opts);

  ::nazg::agent::Options opts;
  if (const char *port = std::getenv("NAZG_AGENT_PORT")) {
    opts.port = static_cast<std::uint16_t>(std::stoi(port));
  }
  if (const char *addr = std::getenv("NAZG_AGENT_ADDR")) {
    opts.bind_address = addr;
  }

  ::nazg::agent::Runtime runtime(opts, logger.get());
  if (!runtime.start()) {
    std::cerr << "Failed to start agent runtime" << std::endl;
    return 1;
  }

  std::cout << "nazg-agent listening on " << opts.bind_address << ":" << opts.port << std::endl;

  while (runtime.is_running() && !g_shutdown.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::cout << "Shutting down nazg-agent..." << std::endl;
  runtime.stop();

  return 0;
}

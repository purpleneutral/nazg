#include "agent/runtime.hpp"
#include "blackbox/logger.hpp"
#include "blackbox/options.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

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

  // Block until signal (Ctrl-C) - basic loop
  while (runtime.is_running()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}

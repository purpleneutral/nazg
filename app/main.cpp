#include "blackbox/logger.hpp"
#include "blackbox/options.hpp"
#include "engine/runtime.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

int main(int argc, char **argv) {

  nazg::engine::options rt{};
  nazg::engine::runtime engine{rt};
  engine.init_logging();

  if (auto *log = engine.logger()) {
    log->info("App", "nazg starting");

    std::ostringstream args;
    args << "argv[0]=" << (argc > 0 ? argv[0] : "<none>");
    for (int i = 1; i < argc; ++i) {
      args << ", argv[" << i << "]=" << argv[i];
    }
    log->debug("App", args.str());
  }

  engine.init_nexus();
  engine.init_commands();

  // Log startup (will only show if console logging is enabled)
  if (auto *log = engine.logger()) {
    log->debug("Engine", "Engine initialization complete");
    log->debug("Engine",
               "console_colors=" + std::to_string(rt.log.console_colors));
  }

  int result = engine.dispatch(argc, argv);

  if (auto *log = engine.logger()) {
    log->info("App", "nazg exiting with code " + std::to_string(result));
  }

  return result;
}

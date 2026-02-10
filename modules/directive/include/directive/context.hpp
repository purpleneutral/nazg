#pragma once
#include <string>
#include <vector>

namespace nazg {
namespace blackbox {
class logger;
}
namespace nexus {
class Store;
}
namespace config {
class store;
}
// namespace palantir {
// struct args;
// } // namespace palantir
} // namespace nazg

namespace nazg::directive {

class registry;

struct context {
  int argc = 0;
  char **argv = nullptr;

  // parsed top-level args (palantír)
  // const ::nazg::palantir::args *parsed = nullptr;

  // logger (Westmarch)
  ::nazg::blackbox::logger *log = nullptr;

  // database (Nexus)
  ::nazg::nexus::Store *store = nullptr;

  // configuration
  ::nazg::config::store *cfg = nullptr;

  // handy: program name and raw trailing tokens (after command)
  std::string prog;
  std::vector<std::string> positionals;

  bool verbose = false;
  const registry *reg = nullptr;
};

} // namespace nazg::directive

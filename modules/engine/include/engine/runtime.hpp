#pragma once
#include <memory>
#include <string>
#include <vector>

#include "blackbox/logger.hpp"
#include "blackbox/options.hpp"

// namespace nazg {
// namespace entmoot {
// class PluginLoader;
// class CapabilityRegistry;
// class EventBus;
// } // namespace entmoot
// } // namespace nazg

namespace blackbox {
class logger;
}
namespace nazg::directive {
class registry;
struct context;
} // namespace nazg::directive
namespace nazg::config {
class store;
}
namespace nazg::nexus {
class Store;
}

namespace nazg::engine {

struct options {
  ::nazg::blackbox::options log;
  bool verbose = false;
  std::string extra_plugin_path;
  // std::string config_path;
};

class runtime {
public:
  explicit runtime(const options &opts);
  ~runtime();

  // boots logging (blackbox)
  void init_logging();
  ::nazg::blackbox::logger *logger() const;

  // Commands (Directive)
  void init_commands();
  directive::registry &registry();
  int dispatch(int argc, char **argv);

  // Config access
  const config::store *config() const;

  // Database (Nexus)
  void init_nexus();
  nexus::Store *nexus() const;

  // loads plugins after logging is ready
  // void bootstrap_plugins(char **argv);

  // int run(int argc, char **argv); // entry point, returns exit code

private:
  struct impl;
  std::unique_ptr<impl> p_;
};

} // namespace nazg::engine

#pragma once
#include "bot/types.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nazg {
namespace config { class store; }
namespace nexus { class Store; }
namespace blackbox { class logger; }
}

namespace nazg::bot {

// Forward declarations
class BotBase;

// Bot factory function type
using BotFactory = std::function<std::unique_ptr<BotBase>(
    const HostConfig&,
    ::nazg::config::store*,
    ::nazg::nexus::Store*,
    ::nazg::blackbox::logger*)>;

// Registry for bot types
class registry {
public:
  registry() = default;

  // Register a bot type
  void register_bot(const BotSpec& spec, BotFactory factory);

  // Get bot specification
  std::optional<BotSpec> get_spec(const std::string& name) const;

  // List all registered bots
  std::vector<BotSpec> list_bots() const;

  // Create bot instance
  std::unique_ptr<BotBase> create_bot(
      const std::string& name,
      const HostConfig& host,
      ::nazg::config::store* cfg,
      ::nazg::nexus::Store* store,
      ::nazg::blackbox::logger* log) const;

private:
  std::map<std::string, BotSpec> specs_;
  std::map<std::string, BotFactory> factories_;
};

// Base class for all bots
class BotBase {
public:
  virtual ~BotBase() = default;

  // Execute bot on configured host
  virtual RunResult execute() = 0;

  // Get bot name
  virtual std::string name() const = 0;

protected:
  BotBase(const HostConfig& host,
          ::nazg::config::store* cfg,
          ::nazg::nexus::Store* store,
          ::nazg::blackbox::logger* log)
      : host_(host), cfg_(cfg), store_(store), log_(log) {}

  HostConfig host_;
  ::nazg::config::store* cfg_;
  ::nazg::nexus::Store* store_;
  ::nazg::blackbox::logger* log_;
};

// Get global bot registry
registry& get_registry();

// Register built-in bots (called at startup)
void register_builtin_bots(registry& reg);

} // namespace nazg::bot

#pragma once
#include "context.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nazg { namespace blackbox { class logger; } }

namespace nazg::directive {

// Execution context passed to commands (globals you care about)
// struct context {
//   nazg::westmarch::logger* logger = nullptr;
//   int   jobs       = 0;
//   bool  no_color   = false;
//   bool  dry_run    = false;
// };

// argv slice for the dispatched command
struct command_context {
  int argc = 0;
  const char* const* argv = nullptr;
};

// Option metadata (used by `info` to render docs)
struct option_spec {
  std::string name;           // e.g. "--json"
  std::string value_name;     // e.g. "FILE" (empty if flag)
  std::string description;    // help text
  bool        required   = false;
  bool        repeatable = false;
  std::string default_value;  // if any
  std::string env_var;        // if any
};

// One command definition
struct command_spec {
  std::string name;                 // "info"
  std::string summary;              // one-liner
  std::string long_help;            // optional longer help
  std::vector<option_spec> options; // zero or more option specs

  // Command callable
  using fn_t = int(*)(const command_context&, const context&);
  fn_t run = nullptr;
};

// directive registry: add, dispatch, and introspect commands
class registry {
public:
  // Add (overwrites if same name)
  void add(const command_spec& spec);

  // Convenience: minimal add
  void add(std::string name, std::string summary, command_spec::fn_t fn);

  // Dispatch by name; returns {found, exit_code}
  std::pair<bool,int> dispatch(std::string_view name,
                               const context& ectx,
                               const std::vector<const char*>& argv) const;

  // Print compact help table
  void print_help(const char* prog) const;

  // Introspection for built-ins like `info`
  const command_spec* find(std::string_view name) const;
  const std::vector<std::string>& order() const { return order_; }
  const std::unordered_map<std::string, command_spec>& all() const { return map_; }

private:
  std::unordered_map<std::string, command_spec> map_;
  std::vector<std::string> order_; // insertion order for pretty output
};

} // namespace nazg::directive

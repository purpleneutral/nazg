#pragma once
#include <functional>
#include <string>

namespace nazg::directive {

struct context;

using handler_fn = std::function<int(const context &)>;

struct command {
  std::string name;    // e.g. "info"
  std::string summary; // one-line help
  handler_fn handler;  // the code to run
};

} // namespace nazg::directive

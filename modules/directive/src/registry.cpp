#include "directive/registry.hpp"
#include <cstdio>
#include <iostream>

namespace nazg::directive {

void registry::add(const command_spec &spec) {
  const bool existed = map_.find(spec.name) != map_.end();
  map_[spec.name] = spec;
  if (!existed)
    order_.push_back(spec.name);
}

void registry::add(std::string name, std::string summary,
                   command_spec::fn_t fn) {
  command_spec s{};
  s.name = std::move(name);
  s.summary = std::move(summary);
  s.run = fn;
  add(s);
}

std::pair<bool, int>
registry::dispatch(std::string_view name, const context &ectx,
                   const std::vector<const char *> &argv) const {
  auto it = map_.find(std::string(name));
  if (it == map_.end())
    return {false, 2};

  command_context cctx{};
  cctx.argc = static_cast<int>(argv.size());
  cctx.argv = argv.data(); // no cast

  int code = 0;
  if (it->second.run)
    code = it->second.run(cctx, ectx);
  return {true, code};
}

const command_spec *registry::find(std::string_view name) const {
  auto it = map_.find(std::string(name));
  if (it == map_.end())
    return nullptr;
  return &it->second;
}

void registry::print_help(const char *prog) const {
  std::fprintf(stderr, "usage: %s <command> [options]\n\n", prog);
  std::fprintf(stderr, "commands:\n");
  size_t colw = 0;
  for (auto &n : order_)
    colw = std::max(colw, n.size());
  for (auto &n : order_) {
    auto &s = map_.at(n);
    std::fprintf(stderr, "  %-*s  %s\n", (int)colw, s.name.c_str(),
                 s.summary.c_str());
  }
  std::fprintf(
      stderr,
      "\nUse: %s info [--json|--table] [--only=name,...] [--skip=name,...]\n",
      prog);
}

} // namespace nazg::directive

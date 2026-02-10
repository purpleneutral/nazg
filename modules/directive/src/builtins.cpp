#include "directive/builtins.hpp"
#include "directive/registry.hpp"
#include "blackbox/logger.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace nazg::directive {

static std::vector<std::string> split_csv(std::string s) {
  std::vector<std::string> out;
  std::stringstream ss(std::move(s));
  std::string item;
  while (std::getline(ss, item, ','))
    if (!item.empty())
      out.push_back(item);
  return out;
}

static void print_table(const registry &reg, const std::set<std::string> &only,
                        const std::set<std::string> &skip) {
  // compute column width
  size_t colw = 0;
  for (auto &n : reg.order())
    colw = std::max(colw, n.size());

  std::cout << "COMMANDS\n";
  for (auto &n : reg.order()) {
    if (!only.empty() && !only.count(n))
      continue;
    if (skip.count(n))
      continue;
    auto *spec = reg.find(n);
    if (!spec)
      continue;
    std::cout << "  " << std::left << std::setw((int)colw) << n << "  "
              << spec->summary << "\n";

    // print options if any
    for (auto &opt : spec->options) {
      std::ostringstream left;
      left << "    " << opt.name;
      if (!opt.value_name.empty()) {
        left << " " << opt.value_name;
      }
      std::ostringstream right;
      right << opt.description;
      if (opt.required)
        right << " (required)";
      if (opt.repeatable)
        right << " (repeatable)";
      if (!opt.default_value.empty())
        right << " [default: " << opt.default_value << "]";
      if (!opt.env_var.empty())
        right << " [env: " << opt.env_var << "]";
      std::cout << std::left << std::setw((int)colw + 4) << left.str() << "  "
                << right.str() << "\n";
    }
  }
}

static void print_json(const registry &reg, const std::set<std::string> &only,
                       const std::set<std::string> &skip) {
  std::cout << "{\n  \"commands\": [\n";
  bool first_cmd = true;
  for (auto &n : reg.order()) {
    if (!only.empty() && !only.count(n))
      continue;
    if (skip.count(n))
      continue;
    auto *spec = reg.find(n);
    if (!spec)
      continue;

    if (!first_cmd)
      std::cout << ",\n";
    first_cmd = false;
    std::cout << "    {\n"
              << "      \"name\": " << "\"" << n << "\",\n"
              << "      \"summary\": " << "\"" << spec->summary << "\",\n"
              << "      \"options\": [";
    bool first_opt = true;
    for (auto &o : spec->options) {
      if (!first_opt)
        std::cout << ", ";
      first_opt = false;
      std::cout << "{"
                << "\"name\":\"" << o.name << "\","
                << "\"value\":\"" << o.value_name << "\","
                << "\"desc\":\"" << o.description << "\","
                << "\"required\":" << (o.required ? "true" : "false") << ","
                << "\"repeatable\":" << (o.repeatable ? "true" : "false") << ","
                << "\"default\":\"" << o.default_value << "\","
                << "\"env\":\"" << o.env_var << "\""
                << "}";
    }
    std::cout << "]\n    }";
  }
  std::cout << "\n  ]\n}\n";
}

// directive info implementation
static int cmd_info(const command_context &ctx, const context &ectx) {
  if (ectx.log)
    ectx.log->enable_tui_mode(true);

  bool want_json = false;
  bool want_table = true;
  bool nologo = false;
  std::set<std::string> only, skip;

  // Parse: [--json|--table] [--only=a,b] [--skip=a,b] [--nologo]
  for (int i = 2; i < ctx.argc; ++i) {
    std::string v = ctx.argv[i];
    if (v == "--json") {
      want_json = true;
      want_table = false;
    } else if (v == "--table") {
      want_table = true;
      want_json = false;
    } else if (v == "--nologo") {
      nologo = true;
    } else if (v == "--only" && i + 1 < ctx.argc) {
      for (auto &s : split_csv(ctx.argv[++i]))
        only.insert(s);
    } else if (v.rfind("--only=", 0) == 0) {
      for (auto &s : split_csv(v.substr(7)))
        only.insert(s);
    } else if (v == "--skip" && i + 1 < ctx.argc) {
      for (auto &s : split_csv(ctx.argv[++i]))
        skip.insert(s);
    } else if (v.rfind("--skip=", 0) == 0) {
      for (auto &s : split_csv(v.substr(6)))
        skip.insert(s);
    }
  }

  if (nologo)
    ::setenv("NAZG_INFO_NOLOGO", "1", 1);

  // Introspect registry via a backdoor: put it in argv[1] as opaque pointer
  // (We wire this in registry::dispatch caller; safer than globals.)
  // argv[1] holds a stringified pointer to registry (const registry*)
  const registry *reg = ectx.reg;
  if (!reg) {
    std::cerr << "directive: info: registry not provided in context\n";
    return 2;
  }

  // if (ctx.argc < 2) {
  //   std::cerr << "edict: info: ctx missing registry ptr\n";
  //   return 2;
  // }
  // const registry *reg = nullptr;
  // {
  //   std::uintptr_t v = 0;
  //   std::stringstream ss;
  //   ss << std::hex << ctx.argv[1];
  //   ss >> v;
  //   reg = reinterpret_cast<const registry *>(v);
  // }
  // if (!reg) {
  //   std::cerr << "edict: info: invalid registry ptr\n";
  //   return 2;
  // }
  //
  if (want_json)
    print_json(*reg, only, skip);
  if (want_table)
    print_table(*reg, only, skip);
  return 0;
}

void register_info(registry &r) {
  command_spec s{};
  s.name = "commands";
  s.summary = "List all available commands and their options (supports --json/--table, "
              "--only, --skip, --nologo)";
  s.options = {
      {"--json", "", "Output JSON instead of table", false, false, "", ""},
      {"--table", "", "Force table output", false, false, "", ""},
      {"--only", "NAMES", "CSV of command names to include", false, false, "",
       ""},
      {"--skip", "NAMES", "CSV of command names to exclude", false, false, "",
       ""},
      {"--nologo", "", "Hide ASCII logo if any", false, false, "", ""},
  };
  s.run = &cmd_info;
  r.add(s);
}

} // namespace nazg::directive

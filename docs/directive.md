# Directive Framework (`modules/directive`)

The directive module underpins Nazg's command system. It stores command definitions, exposes a dispatch API,
and powers both CLI invocations and assistant menus. Every command a module registers is ultimately executed
through this framework.

---

## Quick Reference

**Register a Command:**
```cpp
int my_handler(const directive::command_context& ctx,
               const directive::context& ectx) {
  // Access shared services
  auto *log = ectx.log;
  auto *store = ectx.store;
  auto *cfg = ectx.cfg;
  // Command logic here
  return 0;
}

void my_module::register_commands(directive::registry& reg, directive::context& ctx) {
  reg.add("my-cmd", "Brief description", my_handler);
}
```

**Dispatch:**
```cpp
auto [found, exit_code] = reg.dispatch("my-cmd", context, argv);
if (!found) { /* handle unknown command */ }
```

**Context Fields:**
- `ectx.log` - Blackbox logger
- `ectx.store` - Nexus persistence
- `ectx.cfg` - Config store
- `ectx.verbose` - Verbose flag
- `ectx.argc/argv` - Full command line

---

## 1. Responsibilities

- **Registration** – Provide a simple API for modules to add commands (`name`, `summary`, handler, option
  metadata).
- **Dispatch** – Look up commands by name from the CLI and invoke them with a shared execution context.
- **Introspection** – Supply metadata for help output, assistant action cards, and future tooling such as shell
  completions.

---

## 2. Core Types

| Type | Description |
|------|-------------|
| `directive::command_context` | Slice of `argc/argv` relevant to the command (e.g., `nazg status` → argv[0..]). |
| `directive::context` | Shared runtime resources available to handlers (logger, Nexus store, config pointer, verbose flag, registry pointer, CLI argv). |
| `directive::option_spec` | Metadata describing a single CLI option (name, value name, description, flags). |
| `directive::command_spec` | Fully described command entry (name, summary, long help, options, handler function). |
| `directive::registry` | Container that stores command specs, prints help, and dispatches handlers. |

Handlers use the signature `int handler(const command_context&, const context&)`. Return values propagate back to
the engine dispatcher.

---

## 3. Typical Registration Pattern

```cpp
namespace {
int cmd_status(const directive::command_context& ctx,
               const directive::context& ectx) {
  // Command logic…
  return 0;
}
}

void git::register_commands(directive::registry& reg, directive::context& ctx) {
  reg.add("git-status", "Display git summary", cmd_status);

  directive::command_spec spec{};
  spec.name = "git-sync";
  spec.summary = "Fetch and rebase onto upstream";
  spec.long_help = "Usage: nazg git-sync [--force]";
  spec.options.push_back({"--force", "", "Allow rewriting local commits"});
  spec.run = cmd_git_sync;
  reg.add(spec);
}
```

Engine calls each module’s `register_commands` function during bootstrap so that commands are available for
subsequent dispatch.

---

## 4. Dispatch Flow

1. Engine populates a `directive::context` with shared resources.
2. `runtime::dispatch` determines the command name (`argv[1]`).
3. `registry.dispatch(name, context, argv_vector)` searches the map. When found it calls the handler and returns
   `{true, exit_code}`. Unknown commands result in `{false, 2}` and a help printout.
4. Handlers read args from `command_context` (`ctx.argv[i]`) and use `directive::context` for logging or
   persistence.

Commands should log errors via `ectx.log` and print user-facing messages to standard error/output as needed.

---

## 5. Context Fields

| Field | Purpose |
|-------|---------|
| `int argc` / `char** argv` | Original CLI vector (useful for global-option parsing). |
| `std::string prog` | Program name (normally `nazg`). |
| `::nazg::blackbox::logger* log` | Shared logger, may be `nullptr` before logging initialises. |
| `::nazg::nexus::Store* store` | Persistence layer for commands that need durable state. |
| `const ::nazg::config::store* cfg` | Read-only access to configuration values. |
| `bool verbose` | Reflects CLI `--verbose` flag for conditional output. |
| `registry* reg` | Back-reference when commands need to introspect (e.g., help commands). |

Guide commands to prefer these shared resources over creating their own instances.

---

## 6. Help & Introspection

- `registry.print_help(prog)` prints a two-column command table using insertion order so related commands stay
  grouped.
- `registry.find(name)` returns a pointer to a spec, enabling commands like `nazg info` or the assistant to fetch
  long help and option metadata.
- `registry.order()` exposes the insertion order vector when custom formatting is needed.

This metadata will also feed shell completion generators and richer assistant experiences.

---

## 7. Error Handling Guidance

- Return non-zero exit codes on failure so scripts can detect and react accordingly.
- Always log explainers through `ectx.log` before returning an error.
- For user input errors, prefer descriptive `std::cerr` output or prompt-based corrections.
- Stick to deterministic behaviour; avoid reading from global state outside the supplied context.

---

## 8. Future Enhancements

- **Subcommands** – Support hierarchical names (`nazg git status`) by adding nested registries or automatic
  prefix handling.
- **Argument parsing helpers** – Leverage `option_spec` to auto-parse options and generate usage text.
- **Dynamic loading** – Reintroduce the plugin loader (Entmoot) so modules can register commands at runtime.
- **Analytics** – Record command invocations through Nexus for usage insights.
- **Shell completions** – Export registry data to generate completions for bash/zsh/fish.

---

## 9. Troubleshooting

| Issue | Cause | Resolution |
|-------|-------|-----------|
| “Unknown command” despite registration | Registration function never called | Ensure `init_commands()` invokes the module’s `register_commands`. |
| Command missing logger/store | Context not populated | Verify engine filled `directive::context` before dispatch. |
| Help output truncated | Long summaries without spacing | Keep summaries concise or expand to long help. |
| Need shared state between commands | Overloading directive context | Persist via Nexus or extend the context struct intentionally. |

Directive keeps Nazg’s command surface predictable and discoverable. Register commands consistently and the
assistant, CLI, and future tooling will all benefit from the same source of truth.

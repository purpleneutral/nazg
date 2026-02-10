# Engine Runtime (`modules/engine`)

The engine module is Nazg's orchestrator. It constructs the runtime, wires subsystems together, registers CLI
commands, and funnels user input through either direct commands or the interactive assistant. Every Nazg
execution flows through this runtime.

---

## Quick Reference

**Bootstrap Sequence:**
```cpp
nazg::engine::options opts;
nazg::engine::runtime engine(opts);
engine.init_logging();    // Initialize blackbox logger
engine.init_nexus();      // Open database, run migrations
engine.init_commands();   // Register all commands
return engine.dispatch(argc, argv);
```

**Built-in Commands:**
- `nazg update [--ref REF] [--rollback]` - Update/rollback binary
- `nazg status` - Show project summary
- `nazg info` - Display system information

**Environment Variables:**
- `NAZG_LOG_CONSOLE=1` - Enable console logging
- `NAZG_REPO=<url>` - Override git repo URL

**Access Shared Services:**
```cpp
auto *logger = engine.logger();
auto *config = engine.config();
auto *store = engine.nexus();
auto &registry = engine.registry();
```

---

## 1. Responsibilities

- **Bootstrap core services** – Initialise logging (Blackbox), configuration (config::store), and persistence
  (Nexus) before handing off to higher-level modules.
- **Command wiring** – Allocate a directive registry, populate the shared context, and register both engine-level
  commands (`update`, `status`, `info`) and module-provided commands (`brain`, `git`, `scaffold`, `bot`, etc.).
- **Assistant mode** – When no command is provided, run the interactive assistant that inspects the workspace
  and suggests next actions.
- **Dispatch** – Interpret CLI arguments, locate the requested command, and propagate exit codes back to the
  shell.
- **Shared resource provider** – Expose getters for logger, config store, and Nexus connection so other modules
  can reuse them safely.

---

## 2. Engine Options

`modules/engine/include/engine/runtime.hpp` defines the configuration passed into the runtime constructor:

```cpp
struct options {
  ::nazg::blackbox::options log; // seed options before config/env overrides
  bool verbose = false;          // force console logging
  std::string extra_plugin_path; // reserved for future plugin loader
};
```

Options are merged with config and environment values during `init_logging()`. The `--verbose` CLI flag maps to
`options::verbose`.

---

## 3. Bootstrap Sequence

Call order from `app/main.cpp`:

```cpp
nazg::engine::runtime engine(opts);
engine.init_logging();
engine.init_nexus();
engine.init_commands();
return engine.dispatch(argc, argv);
```

### 3.1 Logging (`init_logging`)
1. Read overrides from `config::store` `[blackbox]` section if present.
2. Apply environment overrides (e.g., `NAZG_LOG_CONSOLE=1`).
3. Honour CLI `--verbose`.
4. Construct `blackbox::logger` and give it back to `config::store` via `set_logger`.
5. Reload config so the load operation is logged using the newly initialised logger.

### 3.2 Nexus (`init_nexus`)
1. Build `nexus::Config` from the config store (defaults to XDG data dir).
2. Call `nexus::Store::create` to open the database.
3. Run `Store::initialize()` to execute migrations and enable WAL mode.
4. Store the pointer for later use and inject it into the directive context.

### 3.3 Commands (`init_commands`)
1. Allocate a `directive::registry` and `directive::context`.
2. Populate context fields: logger, Nexus store, config pointer, verbose flag, registry pointer.
3. Register engine commands:
   - `update` – Build or roll back the Nazg binary via `modules/engine/src/updater.cpp`.
   - `status` – Print the current workspace summary (detector + git insights).
   - `info` – Output runtime metadata (OS, versions).
4. Invoke module registration hooks in a consistent order so help output remains predictable:
   `brain::register_commands`, `scaffold::register_commands`, `git::register_commands`, `bot::register_commands`,
   `prompt::register_demo_command`, etc.

---

## 4. Assistant Mode

When `dispatch` sees no command argument, it enters assistant mode (`run_assistant_mode` in `runtime.cpp`).

Flow:
1. Determine current directory and log it.
2. Ask `brain::Detector` for `ProjectInfo` and record facts (language, build system, SCM, tool list).
3. Use `git::Client` to determine git status (branch, upstream, divergence, change counts).
4. Build a `prompt::Prompt` card containing facts and context-sensitive guidance.
5. Assemble an actions list (e.g., “Show all available commands”, “View project status summary”,
   “🚀 Create a new project…”, “Commit changes”, “Build project”). Suggestions depend on workspace state.
6. Display the menu, record the choice, and inform the user how to run the selected command manually.

Assistant mode currently acts as a guided navigator; it does not execute the command automatically. This keeps
behaviour predictable for both interactive and scripted use.

---

## 5. Dispatch Mechanics

`runtime::dispatch(int argc, char** argv)` performs the following:
- Populate the directive context with the incoming `argc`, `argv`, and program name.
- Handle global help (`nazg --help`) by printing the registry help table and returning success.
- Extract the command name (`argv[1]` when present). Empty command launches the assistant.
- Delegate to `registry.dispatch`, which resolves the command and invokes the handler.
- On unknown command, warn via logger, show help, and return exit code 2.

The dispatcher intentionally keeps parsing minimal today. Future enhancements may strip global flags before
calling into the registry.

---

## 6. Shared Accessors

| Method | Description |
|--------|-------------|
| `blackbox::logger* runtime::logger()` | Retrieve the shared logger (nullable until `init_logging`). |
| `const config::store* runtime::config()` | Read-only pointer to the config store. |
| `nexus::Store* runtime::nexus()` | Access to persistence layer (nullable until `init_nexus`). |
| `directive::registry& runtime::registry()` | Access to the command registry after initialisation. |

Modules should always obtain these through the runtime or directive context instead of storing their own copies.

---

## 7. Engine + Module Interactions

| Module | How the engine collaborates |
|--------|-----------------------------|
| Config (`modules/config`) | Created immediately so logging options can be fetched before logger construction. |
| Nexus (`modules/nexus`) | Opened during bootstrap and passed to directive context for use by commands. |
| Directive (`modules/directive`) | Registry/context allocated and shared with all module registrations. |
| Brain (`modules/brain`) | Used directly in assistant mode and by the `status` command for project detection. |
| Git (`modules/git`) | Provides CLI commands and assistant data; engine passes logger and store through context. |
| Scaffold (`modules/scaffold`) | Registers project creation commands consumed by assistant suggestions. |
| Task (`modules/task`) | Builds generated plan outputs (via assistant or future automation). |
| Bot (`modules/bot`) | Registers bot-related commands; nexus pointer enables persistence of bot hosts/runs. |
| Prompt (`modules/prompt`) | Assistant relies on prompt rendering utilities to present options. |

---

## 8. Updater Command

`cmd_update` (defined inside `runtime.cpp`) integrates the updater module:
- Parses flags such as `--ref`, `--prefix`, `--rollback`, `--dry-run`, `--no-local`, etc.
- Delegates to `update::update_from_source` or `update::rollback`.
- Logs success/failure and prints a concise human-friendly summary (`✓ updated to vX.Y` or `✗ Update failed`).

See `modules/engine/src/updater.cpp` for the heavy lifting (building Nazg, copying artefacts, rotating versions).

---

## 9. Future Work

- **Global flag parsing** – Recognise patterns like `nazg --verbose status` and apply options before dispatch.
- **Plugin loading** – Re-enable the Entmoot plugin loader so external modules can register commands without
  recompilation.
- **Assistant automation** – Allow assistant choices to optionally execute commands immediately.
- **Session analytics** – Record assistant usage statistics in Nexus for telemetry.
- **Graceful fallbacks** – Detect non-interactive terminals and emit plain-text summaries instead of prompt UI.

---

## 10. Troubleshooting

| Symptom | Cause | Resolution |
|---------|-------|-----------|
| “Directive not initialized” | `init_commands()` skipped before `dispatch()` | Ensure bootstrap sequence matches `main.cpp`. |
| No console logs with `--verbose` | Logger not reinitialised yet | Call `init_logging()` before running work. |
| Database errors during init | Invalid `nexus` path or missing permissions | Adjust `[nexus] db_path` or fix directory ownership. |
| Assistant shows no suggestions | Detector could not infer language/build | Check project structure or extend Brain heuristics. |
| `update` fails with git errors | Local repo missing or wrong ref | Provide `--src` to local tree or ensure `cfg.repo_url` is reachable. |

The engine keeps Nazg coherent: initialise it carefully, register commands consistently, and every module will
inherit the same logging, configuration, and persistence guarantees.

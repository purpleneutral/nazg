# Nazg

Nazg is a modular developer assistant that keeps track of your projects, reasons about what to do next, and
explains its decisions. Instead of a one-off setup script, Nazg bundles a runtime, planner, CLI, and persistent
state into a single binary that can bootstrap environments, run project workflows, and surface context-aware
advice.

## Why Nazg?

- **Project memory** – Remembers facts, snapshots, and prior runs through the `nexus` persistence layer.
- **Explainable planning** – The `brain` module detects your workspace, plans builds/tests, and records the
  rationale for each suggestion.
- **Extensible runtime** – Subsystems add commands and behaviours through the `directive` registry; new modules
  can be dropped in without touching the core.
- **Portable binary** – Delivered as a self-contained executable that configures logging, persistence, and
  plugins on startup.
- **Guided UX** – Offers both direct CLI commands and an interactive assistant mode powered by the `prompt`
  module.

## Architecture at a Glance

```
+-----------+        +-----------+        +-----------+
|  Engine   | -----> | Directive | <----- |  Modules  |
+-----------+        +-----------+        +-----------+
      |                    |                   |
      v                    v                   v
  Logging / Config     CLI Commands         Brain, Git,
  (`blackbox`,          (`nazg status`,     Scaffold, Bot,
   `config`)             `nazg update`,     Task, Prompt…
                         ...)
```

### Core Modules

- **engine** – Boots logging (`blackbox`), configuration (`config::store`), persistence (`nexus`), registers
  commands, and dispatches the CLI or assistant mode.
- **directive** – Command registry/dispatcher that modules use to expose new verbs (`nazg status`,
  `nazg build_facts`, etc.).
- **brain** – Detects project context, computes filesystem snapshots, plans actions, and records events/facts.
- **nexus** – SQLite-backed store for projects, snapshots, runs, events, and persistent facts.
- **system** – Cross-platform helpers for filesystem utilities, process execution, package installation, and
  system metadata.
- **prompt** – Simple TUI prompt builder used for assistant interactions and confirmations (e.g., package
  installs).
- **bot / scaffold / git / task** – Optional assistants that add directive commands for automation, scaffolding,
  source control, and task workflow management.

For deeper dives, see the module docs in `docs/`:

- `docs/engine.md` – Runtime lifecycle and command dispatch.
- `docs/brain.md` – Detector, snapshotter, and planner internals.
- `docs/config.md` – Configuration sources and usage.
- `docs/system.md` – OS abstraction utilities.
- `docs/directive.md`, `docs/nexus.md`, `docs/task.md`, …

## Getting Started

### Prerequisites

- C++17 toolchain (GCC 9+ / Clang 10+)
- CMake 3.16 or newer
- SQLite3 development headers (`libsqlite3-dev`)
- libcurl development headers (`libcurl4-openssl-dev`)
- OpenSSL development headers (`libssl-dev`)
- `git` (used by updater and detector)

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The primary binary is produced at `build/nazg` (or `build/bin/nazg` depending on generator).

### Run

- `./build/nazg` – Launch interactive assistant mode that inspects the current directory and suggests actions.
- `./build/nazg status` – Print a quick project summary (language, build system, git status, etc.).
- `./build/nazg build_facts` – Inspect stored facts for the current project.
- `./build/nazg update --help` – Build or rollback Nazg itself using the updater module.

### Usage Examples

Assistant greeting and action selection:

```
$ ./build/nazg
👋 Hi! I'm Nazg. How can I help?
Facts
  Current directory  demo-app
  Language detected  cpp
  Git repository     yes
  Changes            1 modified, 0 staged, 0 untracked

Actions
  0. Show all available commands
  1. View project status summary
  2. Build project
  3. Commit changes
  4. Update nazg
  5. Exit
```

Running a module command directly:

```
$ ./build/nazg git-status
Branch     : feature/bot-framework
Upstream   : origin/feature/bot-framework
Divergence : 2 ahead, 1 behind
Changes    : 3 modified, 0 staged, 1 untracked
```

> **New to nazg?** See the [Quickstart Guide](docs/quickstart.md) for a 60-second introduction.

### Configuration & Data

- Config file: `~/.config/nazg/config.toml` (auto-created on first run if missing).
- Data directory (SQLite DB, logs, versions): `~/.local/share/nazg/` by default. Paths honour XDG variables when
  set.
- Environment overrides: `NAZG_LOG_CONSOLE=1`, `NAZG_REPO=<repo-url>`, etc.

## Extending Nazg

Modules and plugins register themselves through `directive::registry` and gain access to shared services (logger,
config, Nexus store) via the runtime context. A typical plugin integration looks like this:

1. Create a module under `modules/<name>/` with headers in `include/` and implementation in `src/`.
2. Expose `void register_commands(directive::registry&, directive::context&)` to add CLI verbs. Your handlers
   can read/write persistence through `directive::context::store` and log via `context::log`.
3. Leverage existing helpers:
   - Use `prompt::Prompt` for interactive flows.
   - Query `brain::Detector` or `nexus::Store` for project facts instead of duplicating scans.
   - Call `system::install_package` when optional dependencies are missing.
4. Update documentation in `docs/<name>.md` so future maintainers know how to use your module.
5. If you need configuration, document the keys and read them via `config::store` (see `docs/config.md`).

Nazg’s engine will pick up your module when it calls the module’s registration function during bootstrap.

## Integrating Modules & Plugins

The following checklist helps module authors plug into Nazg’s core services:

| Capability | Hook |
|------------|------|
| Logging | Request `ectx.log` or `engine.logger()`; don’t instantiate new loggers. |
| Persistence | Use `ectx.store` (`nexus::Store*`) to persist facts, events, or custom tables. |
| Configuration | Read via `ectx.cfg` (`config::store*`), expanding env vars automatically. |
| Command surface | Register commands through `directive::registry::add`. Keep names kebab-cased (`my-module run`). |
| Assistant cards | Build UI with `prompt::Prompt`, then dispatch existing commands via the registry. |
| Task execution | Convert recommendations into `brain::Plan` instances and hand them to `task::Builder`. |

When targeting remote automation, share bot infrastructure—`docs/bots.md` explains how to register new bots, reuse
SSH transport helpers, and store reports.

## Additional Resources

- `docs/` – Detailed subsystem documentation and design notes.
- `modules/` – Source for each module; headers live in `include/`, implementations in `src/`.
- `build.sh`, `run.sh` – Convenience scripts for local development.

Nazg is an evolving toolkit—feedback and contributions are welcome!

## Smoke Tests

Run the CLI smoke tests to make sure the core commands behave as expected:

```bash
./tests/smoke.sh --build
```

The script builds Nazg (omit `--build` if you already compiled `build/nazg`),
resets an isolated XDG state under `./.tmp/nazg-smoke`, and exercises commands
like `nazg status`, `nazg why`, and `nazg commands`. Each test writes its
captured output to `./.tmp/nazg-smoke/artifacts/<index>-<name>.log` and will
report `[PASS]` or `[FAIL]` markers along with a final summary. Use
`--verbose` to stream command output during the run or `--keep-artifacts` to
compare logs across runs.

If `build/nazg-agent` can bind a local TCP port the suite also runs
`nazg bot doctor` via the embedded agent transport so bot history and
reporting commands are exercised. In sandboxes where socket syscalls are
blocked the harness reports `[SKIP]` for these cases rather than failing.
Run the script on a machine with network permissions to cover the remote bot
flow end-to-end.

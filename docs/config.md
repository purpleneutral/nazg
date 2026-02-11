# Configuration Module (`modules/config`)

Nazg’s configuration subsystem gives every module a consistent view of user preferences while keeping the
implementation intentionally small and dependency-free. The engine constructs a shared `config::store` instance
on startup and passes it to modules that need to read tunables or feature flags.

---

## 1. Goals

- **Centralised access** – A single `config::store` instance is shared by engine, logging, bots, and any other
  subsystem that needs configuration values.
- **Safe defaults** – When the config file does not exist, the store simply reports missing keys so callers can
  fall back to compiled defaults.
- **Environment awareness** – Paths honour XDG directory conventions and config values can expand environment
  variables such as `$HOME` or `${XDG_DATA_HOME}`.
- **Explainability** – Once a logger is injected the store reports which file was loaded and surfaces parsing
  issues.
- **Future layering** – The API leaves room for project-local overrides and CLI-driven updates without breaking
  existing consumers.

---

## 2. Core API Surface

| Function | Description |
|----------|-------------|
| `config::store::store(path = "", logger = nullptr)` | Loads the config from `path` or from the default path when empty. The optional logger is stored for later use. |
| `bool has(section, key) const` | Returns `true` when a value exists. |
| `std::string get_string(section, key, default)` | Retrieves a string, returning `default` if the key is absent. |
| `int get_int(section, key, default)` | Retrieves an integer (with best-effort parsing and logging on failure). |
| `bool get_bool(section, key, default)` | Retrieves a boolean (`true`, `false`, `1`, `0`, `on`, `off`, …). |
| `void reload()` | Re-reads the file using the same path and logger. |
| `void set_logger(blackbox::logger*)` | Adds logging after construction so that load/reload operations emit diagnostics. |
| `std::string default_config_path()` | Resolves the global config location (`$XDG_CONFIG_HOME/nazg/config.toml` or `~/.config/nazg/config.toml`). |
| `std::string default_data_dir()` | XDG data directory helper used by modules like the updater (`~/.local/share/nazg`). |
| `std::string default_state_dir()` / `default_cache_dir()` | Helpers for planned features that need state/cache locations. |
| `std::string expand_env_vars(value)` | Replaces `$VAR`, `${VAR}`, and `${VAR:-fallback}` segments inside config values. |

Headers live in `modules/config/include/config/config.hpp` and `modules/config/include/config/parser.hpp`; the
implementations reside in `modules/config/src/`.

---

## 3. Configuration Sources

`config::store` currently reads a **single** TOML-style file. The engine initialises it with

```
~/.config/nazg/config.toml
```

or `$XDG_CONFIG_HOME/nazg/config.toml` when the environment variable is defined. The store does not generate the
file automatically; if the path is missing it simply loads zero keys and logs a debug message. This keeps the
bootstrap path frictionless while still permitting overrides where desired.

> **Planned**: project-local overlays (e.g., `<project>/.nazg/config.toml`) and CLI-controlled overrides are on the
> roadmap but not yet implemented. Until then, treat every documented key as a global setting.

Environment variables influence behaviour in two ways:

1. XDG environment variables adjust where the config/data directories live.
2. Any value read from the TOML file passes through `expand_env_vars`, allowing references to `$HOME`,
   `${PROJECT_ROOT:-.}`, etc.

---

## 4. Example `config.toml`

```toml
[blackbox]
min_level       = "info"
console_enabled = false
console_colors  = true
file_path       = "${XDG_DATA_HOME:-$HOME/.local/share}/nazg/logs/nazg.log"
rotate_bytes    = 5000000
rotate_files    = 5

[nexus]
db_path = "${XDG_STATE_HOME:-$HOME/.local/state}/nazg/nazg.db"

[engine]
verbose = false
extra_plugin_path = "~/nazg/plugins"

[bots]
default_transport = "ssh"
```

When the engine boots it consults these values before constructing the logger and before initialising the Nexus
store. Missing sections simply lead to defaults:

- Logging falls back to the hard-coded `blackbox::options` values.
- Nexus defaults to `default_data_dir() + "/nexus.db"`.
- Engine options remain whatever the CLI supplied.

> For a complete annotated example with every known key, see `examples/config.toml` in the source tree.

---

## 5. Access Pattern in the Engine

```cpp
nazg::engine::runtime::runtime(const options& opts)
    : p_(std::make_unique<impl>(opts)) {
  p_->cfg = std::make_unique<config::store>();
}

void runtime::init_logging() {
  if (p_->cfg->has("blackbox", "min_level")) {
    p_->opts.log.min_level = blackbox::parse_level(
        p_->cfg->get_string("blackbox", "min_level", "info"));
  }
  // ... additional overrides
  p_->log = std::make_unique<blackbox::logger>(p_->opts.log);
  p_->cfg->set_logger(p_->log.get());
  p_->cfg->reload(); // re-read so we log the outcome once logging is enabled
}
```

Modules that receive `config::store*` through the directive context follow a similar pattern: test for key
existence (or rely on default parameters) and prefer descriptive logging when values are unexpected.

---

## 6. Error Handling & Diagnostics

- **Missing file** – Logged at debug level once a logger is available; otherwise silently ignored.
- **Parse errors** – Logged via `warning` with the line number. Keys that fail to parse fall back to the supplied
  default.
- **Boolean/int parsing** – Non-numeric strings emit a warning and revert to the caller-provided fallback.
- **Environment expansion** – Unknown variables expand to empty strings unless a `${VAR:-fallback}` default is
  provided.

When troubleshooting configuration issues, run `nazg --verbose` to enable console logging and inspect the emitted
load messages.

---

## 7. Extending the Schema

When introducing new configuration knobs:

1. Define the section/key structure and document it here or in a module-specific doc.
2. Add sensible defaults in code so the feature works without user intervention.
3. Use the typed getters with explicit default values (`get_string(section, key, "default")`).
4. Log validation errors or unexpected values to help users recover quickly.
5. Consider whether the setting should eventually live in the Nexus database (`settings` table) versus the TOML
   file.

---

## 8. Current Limitations & Planned Work

| Area | Status | Notes |
|------|--------|-------|
| Project-level overrides | Not yet implemented | Tracked as future work; behaviour described here reflects current single-file loading. |
| Write-back / `nazg config set` | Not available | CLI commands are stubs; configuration is read-only at runtime. |
| Schema validation | Minimal | Free-form keys are accepted; adding validation would improve error messages. |
| Hot reload | Manual | Call `config::store::reload()` after editing the file; there is no file watcher. |

---

## 9. Troubleshooting

| Symptom | Likely Cause | Suggested Fix |
|---------|--------------|---------------|
| `Config file not found` appears in verbose logs | File never created (normal on first run). | Create `~/.config/nazg/config.toml` only if overrides are needed. |
| Updated value not visible | Config file edited but store not reloaded. | Restart Nazg or trigger `store.reload()`. |
| Boolean always false | Value not recognised (`True`, `YES`, etc.). | Use `true`/`false`, `1`/`0`, or `on`/`off`. |
| Environment placeholder literal in logs | Variable missing and no fallback provided. | Use `${VAR:-fallback}` syntax or export the environment variable before starting Nazg. |

---

By keeping the configuration layer predictable and compact, Nazg stays portable while still giving downstream
modules the hooks they need to respect user preferences.

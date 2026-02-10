# Blackbox Logging (`modules/blackbox`)

Blackbox is Nazg’s logging backbone. It provides a single logger instance with consistent formatting, rotation,
and configuration so every module—core or plugin—can emit structured logs without bespoke setup.

---

## 1. Goals

- **Unified policy** – Centralise log level, sinks, and formatting decisions so output stays consistent.
- **Low friction** – Modules request the engine’s `blackbox::logger*` and start logging immediately.
- **Operator friendly** – Balance human-readable console output with rotated file logs for later inspection.
- **Config driven** – Honour config files, environment variables, and CLI flags so users customise verbosity
  without recompiling.

---

## 2. Core Types

| Type | Description |
|------|-------------|
| `blackbox::options` | Configuration struct (min level, console/file toggles, rotation, formatting). |
| `blackbox::logger` | Primary interface with `trace`, `debug`, `info`, `warn`, `error` helpers. |
| `parse_level`, `parse_source_style` | Convert human-readable config strings into enums. |

Engine builds a single `blackbox::logger` during `runtime::init_logging()` and shares the pointer via directive
context, config store, and Nexus.

---

## 3. Configuration Reference

`~/.config/nazg/config.toml` (or `$XDG_CONFIG_HOME/nazg/config.toml`) drives logging behaviour:

```toml
[blackbox]
min_level       = "info"        # trace | debug | info | warn | error
console_enabled = false         # console output (overridden by --verbose)
console_colors  = true
color_in_file   = false         # strip ANSI codes from log file
file_path       = "~/.local/share/nazg/logs/nazg.log"
rotate_bytes    = 5242880       # 5 MiB
rotate_files    = 5
source_style    = "short"       # short | relative | full path in log tag
include_ms      = true
pad_level       = true
include_pid     = true
include_tid     = false
```

Overrides:
- `NAZG_LOG_CONSOLE=1` forces console logging.
- `NAZG_LOG_LEVEL=debug` adjusts the minimum level.
- CLI `--verbose` enables console output regardless of config.

---

## 4. Output Characteristics

| Sink | Behaviour |
|------|-----------|
| Console | Optional colour, aligned tags, human-readable timestamps. Disabled by default to keep CLI quiet. |
| File | Always on; plain text unless `color_in_file=true`. Uses rotation configured by `rotate_bytes`/`rotate_files`. |

Example console line:
```
[2025-02-20 10:55:04.123] [Engine] [INFO ] Nazg started
```

---

## 5. Using the Logger

Modules should obtain the logger from the engine or directive context:

```cpp
void run(const directive::command_context&, const directive::context& ectx) {
  if (auto* log = ectx.log) {
    log->info("Git", "Fetching upstream status");
  }
}
```

Plugins can expose their own registration hook that accepts `directive::context&` so they share the existing
logger and respect user-configured policies.

---

## 6. Behaviour Summary

| Scenario | Console | File | Notes |
|----------|---------|------|------|
| Default install | Off | On | Quiet CLI; inspect `~/.local/share/nazg/logs/nazg.log`. |
| `--verbose` | On | On | Ideal for troubleshooting interactively. |
| `NAZG_LOG_CONSOLE=1` | On | On | Quick toggle without editing config. |
| `console_colors=false` | Monochrome | N/A | Use for CI or limited terminals. |

---

## 7. Integration Hooks

- `config::store::set_logger` lets config loading log parse errors once the logger is available.
- Modules can temporarily bump verbosity via `log->set_min_level(...)` if the API is exposed (keep resets in
  mind).
- For long-running tasks, call `log->flush()` before exit to guarantee file output.

When writing plugins, avoid instantiating a new logger unless you have a compelling reason (e.g., separate log
file); reuse Nazg’s shared instance to integrate with end-user expectations.

---

## 8. Future Enhancements

- Structured sinks (JSON log stream for ingestion pipelines).
- Remote transports (syslog, HTTP collectors).
- Runtime controls (`nazg log level warn`).
- Context metadata (project ID, bot run ID) appended automatically for richer diagnostics.

---

## 9. Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| No console output despite `--verbose` | `init_logging()` not called yet | Ensure module code runs after engine initialises logging. |
| Log file missing | `file_path` unwritable | Adjust path in config or fix directory permissions. |
| Duplicate lines | Multiple loggers created | Reuse engine’s logger; do not instantiate per module. |
| ANSI codes in log file | `color_in_file=true` | Disable the option unless tooling understands ANSI. |

Blackbox keeps Nazg’s voice consistent. Share the logger with every module and plugin to guarantee predictable,
achionable logs.

# Contributing to Nazg

## Build Prerequisites

- C++17 compiler (GCC 9+ or Clang 10+)
- CMake 3.16+
- SQLite3 development headers (`libsqlite3-dev`)
- libcurl development headers (`libcurl4-openssl-dev`)
- OpenSSL development headers (`libssl-dev`)

### Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

To treat warnings as errors (required in CI):

```sh
cmake -S . -B build -DNAZG_WARN_AS_ERR=ON
```

## Project Structure

Nazg is organized into modules under `modules/`. Each module follows this layout:

```
modules/<name>/
  include/<name>/   # Public headers
  src/              # Implementation files
```

All code lives under the `nazg::` namespace. Module-specific code uses `nazg::<module>::` (e.g., `nazg::brain::`, `nazg::task::`).

### Key Modules

| Module | Purpose |
|--------|---------|
| `blackbox` | Logging foundation |
| `config` | Configuration management |
| `system` | OS utilities (process, filesystem) |
| `nexus` | SQLite persistence layer |
| `brain` | Failure detection, pattern matching, recovery |
| `task` | Command execution with output capture and timeout |
| `workspace` | Snapshot / time-machine for project state |
| `engine` | Top-level orchestrator and updater |
| `git` | Git server integration (Gitea, cgit) |
| `agent` | Remote agent daemon |
| `tui` | Terminal UI (FTXUI) |

### Adding a Module

1. Create `modules/<name>/include/<name>/` and `modules/<name>/src/`
2. Add `add_nazg_module(<name> <dependencies...>)` to the root `CMakeLists.txt`

## Code Style

- **snake_case** for functions, variables, and file names
- **PascalCase** for types (classes, structs, enums)
- **2-space indent**, no tabs
- Header guards: `#pragma once`
- Prefer `std::string_view` for non-owning string parameters where appropriate
- Use `nazg::system::shell_quote()` when building shell commands to prevent injection

## Command Registration

New CLI commands are registered through the `directive::registry`. See existing directives in `modules/directive/` for examples.

## Pull Requests

1. Create a feature branch from `main`
2. Keep commits focused and well-described
3. Ensure `cmake --build build` succeeds with `-DNAZG_WARN_AS_ERR=ON`
4. Run `ctest` and verify existing tests pass
5. Add tests for new functionality when practical

# System Module (`modules/system`)

Nazg’s **system** module wraps low-level operating system primitives—filesystem helpers, process execution,
terminal handling, and package management—behind a lightweight, cross-platform façade. Higher-level modules use
these utilities to interact with the host safely without duplicating platform-specific logic.

---

## 1. Responsibilities

- **Process execution** – Launch shell commands, capture output, and normalise exit codes.
- **Filesystem helpers** – Provide small utilities for trimming text, reading files, and expanding `~`.
- **Package management** – Detect the active package manager (pacman/apt/dnf/brew) and optionally install
  dependencies with user confirmation.
- **System metadata** – Surface OS, CPU, and memory information for diagnostics (see `sysinfo` module files).
- **Terminal utilities** – Detect terminal capabilities and assist with formatting prompt output.

---

## 2. Process Execution (`system/process.hpp`)

| Function | Description |
|----------|-------------|
| `int run_command(const std::string& cmd)` | Executes a shell command via `std::system` and returns the decoded POSIX exit status (or raw result on non-POSIX platforms). |
| `std::string run_capture(const std::string& cmd)` | Runs a command with `popen`, returns stdout as a string with the trailing newline removed. |
| `CommandResult run_command_capture(const std::string& cmd)` | Executes a command, returning both the exit code and combined stdout/stderr output in a `CommandResult` struct. |
| `CommandResult run_command_with_timeout(const std::string& cmd, int64_t timeout_ms)` | Executes a command with a deadline. Uses `fork`/`pipe`/`poll`/`waitpid` on Unix. Sends `SIGTERM` then `SIGKILL` on timeout; returns exit code 124 (matching coreutils `timeout` convention). Falls back to `run_command_capture` on non-Unix. |
| `std::string shell_quote(std::string_view value)` | Wraps a string in single quotes, escaping embedded quotes so that user input can be safely injected into shell commands. |

Usage pattern:

```cpp
using namespace nazg::system;

std::string cmd = "git commit -m " + shell_quote(message);
int rc = run_command(cmd);
if (rc != 0) {
  log->error("Git", "Commit failed (" + std::to_string(rc) + ")");
}
```

Always pass user-controlled values through `shell_quote` to avoid command injection.

---

## 3. Filesystem Helpers (`system/fs.hpp`)

| Function | Description |
|----------|-------------|
| `std::string trim(const std::string& s)` | Removes leading/trailing whitespace; handy for normalising command output. |
| `std::string read_file_line(const std::string& path)` | Reads the first line of a file and returns it (empty on failure). |
| `std::string bytes_gib(unsigned long long bytes)` | Formats bytes into GiB for human-readable diagnostics. |
| `std::string expand_tilde(const std::string& path)` | Replaces a leading `~` with the user’s home directory. |
| `std::vector<std::string> wrap_text(const std::string& s, int width)` | Wraps text to the requested width for terminal display. |

These helpers intentionally stay small. More advanced operations (ensuring directories, recursive removal,
permissions) currently live in module-specific code such as the engine updater.

---

## 4. Package Utilities (`system/package.hpp`)

The package helper makes it easy for modules to request external dependencies in a user-friendly way.

1. Detect the package manager (`pacman`, `apt`, `dnf`, or `brew`).
2. Check whether a package is already installed.
3. Prompt the user (via `prompt::Prompt`) for permission to install.
4. Run the appropriate install command, logging the result.

Key API calls:

| Function | Description |
|----------|-------------|
| `PackageManager detect_package_manager()` | Detects the available manager using `command -v`. Falls back to `UNKNOWN` if none match. |
| `bool is_package_installed(const std::string& name)` | Uses manager-specific commands (`pacman -Qi`, `dpkg -s`, etc.) to test installation. |
| `std::vector<PackageInfo> get_install_info(const std::string& name)` | Returns the candidate install commands for all supported managers. |
| `bool install_package(const std::string& name, prompt::Prompt* prompt, blackbox::logger* log)` | Prompts the user and attempts installation via the detected manager. |

Example:

```cpp
if (!nazg::system::is_package_installed("sshpass")) {
  prompt::Prompt confirm(log);
  confirm.title("Doctor Bot dependency");
  confirm.question("Install 'sshpass'?");

  if (!nazg::system::install_package("sshpass", &confirm, log)) {
    throw std::runtime_error("sshpass is required for Doctor Bot");
  }
}
```

> **Security note:** installation commands may invoke `sudo`. Always ensure the user is informed via the prompt
> text before executing.

---

## 5. System Information & Terminal Helpers

- `modules/system/src/sysinfo.cpp` gathers OS name, CPU count, memory figures, etc. These utilities are consumed
  by diagnostic commands (e.g., future Doctor Bot features).
- `modules/system/src/terminal.cpp` provides helpers for detecting terminal width, colour support, and cursor
  behaviour, which the `prompt` module uses to render menus without hard-coding escape sequences.

These components are evolving—consult the source files for the latest API until formal headers are published.

---

## 6. Integration Points

| Module | Usage |
|--------|-------|
| Engine updater | Builds shell commands (`git`, `cmake`) using `shell_quote`, copies files with explicit permission checks, and logs failures. |
| Git client (`modules/git`) | Wraps git commands via `run_command`/`run_capture` to provide status, commit, and configuration helpers. |
| Scaffold module | Executes bootstrap commands (e.g., virtual environment creation) and checks for required tools. |
| Bots | Install runtime dependencies (`sshpass`, `kubectl`, etc.) via `install_package`, surfacing prompts through the assistant UI. |
| Prompt module | Queries terminal capabilities to lay out menus and informational cards cleanly. |

---

## 7. Best Practices

- **Quote everything** – Treat any string that could contain user input as unsafe, even when it “should” be a
  simple filename.
- **Check return codes** – `run_command` normalises exit statuses; treat non-zero results as failures and surface
  them to users with actionable messages.
- **Keep prompts informative** – When installing packages, always include the exact command being run so the user
  can review it.
- **Prefer helpers over reimplementation** – Reuse `expand_tilde`, `trim`, and friends to keep behaviour
  consistent across modules.
- **Avoid long-running shell commands in the main thread** – For future asynchronous workflows consider adding a
  dedicated process wrapper.

---

## 8. Future Enhancements

| Idea | Motivation |
|------|------------|
| `ensure_dir`, `is_writable` helpers in `system::fs` | Centralise directory management currently duplicated in the engine updater. |
| Argument-vector process API | Avoid shells entirely by using `posix_spawn`/`execve` for untrusted input. |
| Async / streaming process output | Forward output in real time while still capturing the full result. |
| Windows support | Extend quoting/command detection to work natively on Windows shells (PowerShell/CMD). |
| Rich sysinfo API | Expose structured host metadata for Doctor Bot diagnostics and assistant summaries. |

---

## 9. Troubleshooting

| Symptom | Diagnosis | Resolution |
|---------|-----------|------------|
| `run_command` returns `-1` | The shell failed to spawn (missing `/bin/sh` or permission issue). | Verify shell availability and consider running the command manually. |
| Captured output missing newline | `run_capture` strips the final newline by design. | Append `"\n"` in code if the newline is required. |
| Package install reports success but binary missing | Command succeeded but package not on `PATH` or post-install verification failed. | Check manager logs, ensure `PATH` includes the install location, rerun `is_package_installed`. |
| Terminal art renders incorrectly | Terminal lacks colour/width support or `TERM` unset. | Use the terminal helpers to detect limitations and fall back to plain text. |

---

By funnelling all host interactions through the system module, Nazg keeps higher-level code portable, testable,
and easier to audit for security concerns.

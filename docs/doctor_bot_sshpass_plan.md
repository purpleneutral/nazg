# Doctor Bot sshpass Auto-Install Plan

## Context
- Nazg’s `system` module exposes process helpers plus a planned package manager API that other modules should reuse instead of rolling bespoke shell snippets.
- Doctor Bot runs remote diagnostics over SSH; its setup command currently bails out when `sshpass` is absent and only prints manual install instructions, so the documented install flow isn’t implemented.

## Implementation Steps
1. **Finish Package Manager Backend**
   - Add `modules/system/src/package.cpp` implementing `detect_package_manager`, `package_manager_name`, `is_package_installed`, and `install_package` declared in `modules/system/include/system/package.hpp` using `run_command`/`run_capture` and `shell_quote` for safety.
   - Detect supported managers (pacman → apt → dnf → brew) via `command -v`, return `UNKNOWN` if none found.
   - Build install command strings (`sudo pacman -S --noconfirm <pkg>`, etc.) and log outcomes when a logger is available.

2. **Hook In Prompt Support**
   - When a `Prompt*` is provided, render a confirmation dialog that names the detected manager and the exact command that will run; fall back to a simple Y/n CLI prompt otherwise.
   - Respect user declines by returning `false`, and pipe successes/failures through the optional logger for traceability.

3. **Integrate With Bot Commands**
   - Replace the manual `which sshpass` check in `modules/bot/src/commands.cpp` with `is_package_installed("sshpass")`; if missing, call `install_package` (passing the directive prompt/logger context) and abort only if installation fails or the user declines.
   - Invoke the same helper before Doctor Bot runs inside `cmd_bot_spawn` so every doctor execution can self-heal when `sshpass` is required for key distribution or scripted runs.

4. **Documentation Refresh**
   - Update `docs/system.md` (package utility section) and the relevant bot documentation to describe the new automatic-install behaviour and user consent flow.

## Testing Strategy
- **Unit Coverage**: Add tests around manager detection/command generation, using PATH overrides or dependency injection, to ensure each backend builds the right command without touching the real system.
- **CLI Flow**: Run `nazg bot setup` with `sshpass` intentionally absent (e.g., PATH masking) to confirm the prompt offers installation, verifies success, and exits cleanly if the user declines.
- **Regression**: Execute `nazg bot doctor` (alias of `nazg bot spawn doctor`) with `sshpass` already installed to confirm the fast-path still works and logs/prompt output remain clean.

This plan can guide implementation once we’re ready to land the sshpass auto-install feature for Doctor Bot.

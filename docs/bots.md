# Bot Framework (`modules/bot`)

Nazg’s bot module runs specialised diagnostics and automation against local or remote hosts. The first built-in
bot (“doctor”) performs system health checks over SSH, but the framework is designed for additional bots and
transports.

---

## 1. Architecture

| Component | Description |
|-----------|-------------|
| `bot::registry` | Stores bot specifications (name, description, required inputs) and factories for constructing bot instances. |
| `bot::BotBase` | Abstract interface implemented by each bot. Defines `execute()` returning `RunResult`. |
| `bot::DoctorBot` | Built-in bot that connects over SSH, runs `modules/bot/scripts/doctor.sh`, and reports CPU, memory, disk, service, and network status. |
| `bot::transport` | Helpers for running remote commands, managing SSH options, and capturing output. |
| `bot::commands` | Registers the `bot` root command and delegates to subcommands (`hosts`, `history`, `manage`, `setup`, `spawn`, `report`, …). |
| `bot::types` | Shared types such as `HostConfig`, `Status`, and `RunResult`.

The engine registers bot commands during bootstrap so the CLI and assistant can trigger them.

---

## 2. CLI Commands

| Command | Purpose |
|---------|---------|
| `nazg bot spawn <bot> --host <target>` | Run a bot immediately. Creates/updates host records in Nexus, executes the bot, stores run metadata, and prints the report. Shortcut: `nazg bot doctor --host …` for the doctor bot. |
| `nazg bot list` | Display recent bot runs (currently focused on the doctor bot). |
| `nazg bot hosts` | Show the known host registry from Nexus, including the last status and run timestamp for each machine. |
| `nazg bot history [--bot] [--host] [--limit]` | Inspect historical runs with optional filters. Pulls data from Nexus so you can track success/failure trends per bot or per host. |
| `nazg bot manage [--host LABEL]` | Interactive prompt for renaming, updating, or deleting bot host entries stored in Nexus. Uses the prompt UI to confirm destructive actions. |
| `nazg bot report <bot> --host <target>` | Retrieve and print the latest stored report for a given bot/host combination. |
| `nazg bot setup --host <target>` | Assist with SSH key distribution and prerequisites so doctor bot can log in non-interactively. |

All commands share the directive context for logging, config access, and persistence.

Tip: Run `nazg bot --help` for a quick summary of these subcommands from the CLI.

---

## 3. Data Persistence

Bots rely heavily on Nexus:
- `bot_hosts` – Host label, address, and serialized SSH configuration. Surface this data with `nazg bot hosts` to confirm registration status.
- `bot_runs` – Start/end timestamps, status, exit code, duration. `nazg bot history` queries this table so you can audit how bots behave over time.
- `bot_reports` – JSON payload captured from the bot’s output.

`bot::commands` automatically adds entries to these tables so the assistant and diagnostics can reference bot
history.

---

## 4. Doctor Bot

Doctor Bot is the initial built-in bot and focuses on host health diagnostics.

- **Transport** – SSH; supports specifying username (`user@host`), port, and key via CLI options or config.
- **Script** – Executes `modules/bot/scripts/doctor.sh` remotely. The script gathers load averages, memory usage,
  disk utilisation, service status, and network reachability, then emits JSON.
- **Report** – JSON stored in `bot_reports` and echoed to the console. The assistant can ingest this payload to
  display warnings or tips.
- **Dependencies** – Uses `ssh` and optionally `sshpass` (Nazg offers to install it via `system::install_package`
  when needed).
- **Retry flow** – If authentication fails, Nazg offers to run `nazg bot setup` interactively and retry the bot run.

### Remote Agent Preview

- **Agent runtime** – `nazg-agent` keeps a persistent channel open on remote hosts (default port `7070`).
- **Handshake detection** – `nazg bot spawn` probes the agent via a hello/ack handshake and records availability.
- **Execution path** – When the agent is reachable, Doctor Bot uses it to run health checks and falls back to
  SSH transparently.
- **Configuration** – Hosts accept `--agent-port <n>` (and matching config keys) so deployments can pick
  alternate ports.

---

## 5. Configuration

`config::store` keys affecting bots:

```toml
[bots]
default_transport = "ssh"
ssh_key = "~/.ssh/id_ed25519"

[bots.hosts.lab]
address = "dev@lab-box"
ssh_key = "~/.ssh/lab-box"
services = "docker,postgres"
```

Command-line options override these values. Host configuration entries are resolved by label when present.

---

## 6. Extending the Framework

1. Implement a new bot deriving from `bot::BotBase` and register it in `register_builtin_bots` (or a plugin
   module) with a `BotSpec` describing capabilities.
2. Use `HostConfig` to describe connection requirements (address, port, credentials, services).
3. Store any output as JSON via `RunResult::json_report` so it is preserved in Nexus.
4. Add CLI verbs or assistant actions so users can launch the bot easily.

Transport backends can also be expanded by adding helpers in `transport.cpp` (e.g., HTTP APIs instead of SSH).

---

## 7. Security Considerations

- Store credentials outside the database when possible; `bot_hosts.ssh_config` intentionally keeps minimal
  metadata. Future work may integrate secrets managers.
- Doctor Bot shell script performs read-only checks. Avoid running destructive operations unless the user opts in.
- Prompt the user before installing third-party tools (e.g., `sshpass`).

---

## 8. Troubleshooting

| Issue | Cause | Resolution |
|-------|-------|-----------|
| `Permission denied` during `nazg bot spawn` | SSH key missing or not authorised | Run `nazg bot setup --host <target>` to copy keys, or update config with the correct key path. |
| No records in `nazg bot list` | Bot never run or database path incorrect | Spawn a bot and verify `nexus` configuration. |
| Report empty | Remote script failed or produced non-JSON output | Inspect stderr/stdout in command output and fix the remote environment. |
| `sshpass` warning | Dependency check failed | Install via prompt or manage authentication manually. |

Nazg’s bot framework brings remote insight into the assistant. As new bots are added, document them here so
users understand capabilities, requirements, and safety constraints.

# Task Module (`modules/task`)

The task module executes the plans produced by Brain. It wraps command invocation with logging, working-directory
management, timing, and persistence so Nazg can both run recommended builds and record what happened.

---

## 1. Current Capabilities

- **Execute build plans** – Interpret `brain::Plan` objects (skip/build/unknown) and run the appropriate command.
- **Shell-aware execution** – Detect when the plan uses `/bin/sh -c …` and hand the command to the shell intact.
- **Working directory management** – Change into the requested directory before running the command and restore
  the original directory afterwards.
- **Telemetry** – Measure duration, capture exit codes, and log progress via Blackbox.
- **Persistence** – Record executed commands and outcomes in Nexus (`commands` table) and append builder events
  for historical context.

---

## 2. Key Types

| Type | Description |
|------|-------------|
| `task::ExecutionResult` | Result struct (`success`, `exit_code`, `duration_ms`, `error_message`). |
| `task::Executor` | Low-level runner that builds shell commands, manages directories, and calls `system::run_command`. |
| `task::Builder` | High-level helper that consumes `brain::Plan`, dispatches through `Executor`, and records outcomes in Nexus. |

Headers live under `modules/task/include/task/` with implementations in `modules/task/src/`.

---

## 3. Example Usage

```cpp
brain::Plan plan = planner.decide(project_id, info, snapshot);

nazg::task::Builder builder(store, log);
auto result = builder.build(project_id, plan);

builder.record_build(project_id, plan, result);
if (!result.success) {
  log->error("Task", result.error_message);
}
```

The builder returns early for `Action::SKIP` and `Action::UNKNOWN`, logging the reason instead of executing
anything.

---

## 4. Execution Details

- `Executor::execute` formats the command and arguments, logs the invocation, optionally changes to the target
  directory, runs `nazg::system::run_command`, measures duration, and restores the original directory.
- `Executor::execute_shell` performs the same steps but hands the raw shell command to `system::run_command`
  without splitting arguments.
- Both variants log success or failure with timing information and exit code.

Timeout support is stubbed (`set_timeout`) and will evolve to handle long-running commands.

---

## 5. Persistence Hooks

`Builder::record_build` stores results so the assistant can explain what happened:
- `store->record_command` captures command, arguments, exit code, and duration in the `commands` table.
- `store->add_event` appends an event tagged `builder` describing success or failure.

This data complements planner events, making it possible to trace “Brain suggested BUILD → Builder executed
command → Exit code 1”.

---

## 6. Roadmap

- **Richer task definitions** – Extend the builder to accept explicit step lists, environment overrides, and
  dependency chains.
- **Streaming output** – Capture stdout/stderr and optionally forward to the prompt or logs in real time.
- **Timeouts and cancellation** – Enforce execution limits and respond to user interrupts gracefully.
- **Retry strategies** – Allow configurable retry logic for flaky tasks.
- **Nexus schema expansion** – Add tables for task steps and artefacts when workflows become more structured.

---

## 7. Troubleshooting

| Symptom | Likely Cause | Resolution |
|---------|--------------|-----------|
| `ExecutionResult::success` false with empty message | Command returned non-zero but no extra context | Inspect console logs; extend plan command to emit details. |
| Working directory not restored | Directory change failed or `getcwd` failed | Ensure Nazg has permission to read the current directory before executing tasks. |
| Plan keeps returning `SKIP` | Brain detected no changes | Confirm snapshot logic or trigger a change before rebuilding. |
| Commands missing from history | `record_build` not called | Always call `record_build` after running the plan, even on failure. |

Task closes the loop between “Nazg thinks we should build” and “Nazg built it and here’s what happened.” As the
assistant grows more proactive, this module will evolve into the generic workflow runner.

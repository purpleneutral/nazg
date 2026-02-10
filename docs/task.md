# Task Module (`modules/task`)

The task module executes the plans produced by Brain. It wraps command invocation with logging, working-directory
management, timing, and persistence so Nazg can both run recommended builds and record what happened.

---

## 1. Current Capabilities

- **Execute build plans** – Interpret `brain::Plan` objects (skip/build/unknown) and run the appropriate command.
- **Shell-aware execution** – Detect when the plan uses `/bin/sh -c …` and hand the command to the shell intact.
- **Output capture** – Both `execute()` and `execute_shell()` capture combined stdout/stderr into
  `ExecutionResult::stdout_output`, enabling downstream modules (recovery, learner, pattern matcher) to analyse
  command output.
- **Thread-safe working directories** – Working directory changes are scoped to the child process by prepending
  `cd <dir> &&` to the shell command, avoiding the process-global `chdir()` race condition.
- **Timeout enforcement** – When `set_timeout()` is configured, commands are executed via
  `system::run_command_with_timeout()` which uses `fork`/`poll`/`waitpid` with `SIGTERM`/`SIGKILL` escalation.
- **Telemetry** – Measure duration, capture exit codes, and log progress via Blackbox.
- **Persistence** – Record executed commands and outcomes in Nexus (`commands` table) and append builder events
  for historical context.

---

## 2. Key Types

| Type | Description |
|------|-------------|
| `task::ExecutionResult` | Result struct (`success`, `exit_code`, `duration_ms`, `stdout_output`, `stderr_output`, `error_message`). |
| `task::Executor` | Low-level runner that builds shell commands, captures output, and enforces timeouts via `system::run_command_capture` / `run_command_with_timeout`. |
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

- `Executor::execute` formats the command and arguments, logs the invocation, prepends `cd <dir> &&` if a
  working directory is specified (scoped to the child shell, avoiding `chdir()` race conditions), runs the
  command via `system::run_command_capture` (or `run_command_with_timeout` when a timeout is set), and maps the
  result into `ExecutionResult` including captured output.
- `Executor::execute_shell` performs the same steps but hands the raw shell command without splitting arguments.
- Both variants redirect stderr into stdout (`2>&1`) so all output lands in `stdout_output`.
- `set_timeout(ms)` configures a per-executor deadline. Commands exceeding it are killed via SIGTERM/SIGKILL
  and return exit code 124.

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
- **Streaming output** – Forward captured output to the prompt or logs in real time while still buffering it.
- **Retry strategies** – Allow configurable retry logic for flaky tasks.
- **Nexus schema expansion** – Add tables for task steps and artefacts when workflows become more structured.

---

## 7. Troubleshooting

| Symptom | Likely Cause | Resolution |
|---------|--------------|-----------|
| `ExecutionResult::success` false with empty message | Command returned non-zero but `stdout_output` should now contain the error | Check `stdout_output` for compiler/tool diagnostics. |
| Working directory not applied | `shell_quote()` rejected the path or directory doesn't exist | Verify the directory exists and its name doesn't contain null bytes. |
| Plan keeps returning `SKIP` | Brain detected no changes | Confirm snapshot logic or trigger a change before rebuilding. |
| Commands missing from history | `record_build` not called | Always call `record_build` after running the plan, even on failure. |

Task closes the loop between “Nazg thinks we should build” and “Nazg built it and here’s what happened.” As the
assistant grows more proactive, this module will evolve into the generic workflow runner.

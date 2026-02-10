# Brain Module (`modules/brain`)

Nazg's Brain is the intelligence layer that scans the workspace, records what it finds, and plans the next
action. It combines directory detection, filesystem snapshotting, and high-level planning so the assistant and
CLI commands can explain why they recommend a build, a test run, or a simple skip.

---

## Quick Reference

**Core Classes:**
- `brain::Detector` - Detects language, build system, SCM from filesystem
- `brain::Snapshotter` - Computes SHA-256 tree hashes, tracks changes
- `brain::Planner` - Generates build/test plans based on project state

**Key Functions:**
```cpp
// Detect project
brain::Detector detector(store, log);
auto info = detector.detect(cwd);
detector.store_facts(project_id, info);

// Snapshot and plan
brain::Snapshotter snap(store, log);
auto snapshot = snap.capture(project_id, cwd);
brain::Planner planner(store, log);
auto plan = planner.decide(project_id, info, snapshot);
```

**CLI Commands:**
- `nazg brain detect` - Show detected project info
- `nazg brain snapshot` - Display current snapshot
- `nazg brain plan` - Show recommended action
- `nazg brain facts [PROJECT_ID]` - List stored facts
- `nazg brain runs [PROJECT_ID]` - Show build history

---

## 1. Responsibilities

- **Environment detection** – Identify the dominant language, build system, source control manager, and notable
  tools in the current tree.
- **Change tracking** – Hash the project tree, count files/bytes, and compare against the previous run to flag
  meaningful changes.
- **Planning** – Decide whether to build, skip, or warn based on the latest snapshot and detected build system.
- **Transparency** – Persist facts, snapshots, and planner decisions in Nexus so every recommendation is
  explainable after the fact.
- **CLI support** – Provide `nazg brain …` commands that surface the raw data for debugging and introspection.

---

## 2. Key Types

| Type | Description |
|------|-------------|
| `brain::ProjectInfo` | Structured result from the detector (language, build system, SCM, tool list, booleans). |
| `brain::SnapshotResult` | Output from snapshotter (tree hash, file count, total bytes, `changed` flag, previous hash). |
| `brain::Plan` | Planner decision comprising `action`, `reason`, `command`, `args`, and `working_dir`. |
| `brain::Action` | Enum of planner actions (`BUILD`, `SKIP`, `UNKNOWN`). |

Headers live under `modules/brain/include/brain/` and implementations under `modules/brain/src/`.

---

## 3. Detector

`brain::Detector` walks the workspace and infers project characteristics.

```cpp
brain::Detector detector(store, log);
auto info = detector.detect(cwd);
detector.store_facts(project_id, info);
```

Highlights:
- Looks for build files such as `CMakeLists.txt`, `Makefile`, `Cargo.toml`, `package.json`, and records the first
  match.
- Counts file extensions (skipping build directories, VCS data, caches) to pick the dominant language. Current
  heuristics cover C/C++, C, Python, Rust, Go, and JavaScript/TypeScript.
- Checks for `.git/` to report SCM usage and to populate the tools list (`cmake`, `make`, `git`, …).
- When `store_facts` is called, writes facts into the `facts` table under the `detector` namespace so other
  modules can reuse them without re-scanning.
- Logs summary information when a logger is available.

The detector intentionally trades precision for speed. If you need to ignore additional directories, extend the
skip list in `detector.cpp` or plan for future configuration keys.

---

## 4. Snapshotter

`brain::Snapshotter` captures the state of the project tree and compares it with the last run.

Workflow:
1. Recursively hash files (SHA-256), skipping generated artefacts (`build/`, `node_modules/`, `.git/`, etc.).
2. Sum file counts and total bytes while walking the tree.
3. Fetch the latest snapshot from Nexus and set `SnapshotResult::changed` true when hashes differ.
4. Persist the new snapshot via `nexus::Store::add_snapshot` and emit log messages summarising the run.

Snapshot data drives planner decisions and assistant messaging (“No changes detected” vs “Files changed”).

---

## 5. Planner

`brain::Planner` converts detector and snapshot results into an actionable plan.

```cpp
brain::Planner planner(store, log);
auto plan = planner.decide(project_id, info, snapshot);
```

Behaviour:
- If `snapshot.changed` is false and a previous hash exists, return `Action::SKIP` with a reason containing the
  hash prefix. An informational event is written to Nexus so the history explains the skip.
- Otherwise generate a build plan matching the detected build system:
  - `cmake` → run configure + build (`cmake -B build -S . && cmake --build build -j4`) via `/bin/sh -c`.
  - `make` → run `make -j4`.
  - `cargo` → run `cargo build --release`.
  - `npm` → run `npm run build`.
  - Unknown systems produce `Action::UNKNOWN` with an explanatory reason.
- Each decision logs to the shared logger and records an event in Nexus for transparency.

The planner intentionally focuses on build decisions today. Future iterations can layer in testing, linting, or
custom project workflows.

---

## 6. Task Module Bridge

`task::Builder::build` consumes `brain::Plan` objects. When Brain suggests a build, the task module executes the
command (respecting shell plans when necessary), records the outcome via `Store::record_command`, and emits
builder events. This closes the loop between “why” (Brain) and “what happened” (Task/Nexus).

---

## 7. Command Surface

Brain registers several directive commands in `modules/brain/src/commands.cpp`. Examples include:
- `nazg brain detect` – Run detector and print the full `ProjectInfo` structure.
- `nazg brain snapshot` – Compute a snapshot and show summary counts.
- `nazg brain plan` – Produce the current plan for the workspace.
- `nazg brain facts|runs|events` – Inspect persisted data for debugging.

Commands reuse the shared directive context, so they have access to the logger, Nexus store, and verbose flag.

---

## 8. Persistence Cheat Sheet

| Table | Writer | Notes |
|-------|--------|-------|
| `projects` | Detector (`ensure_project`) | Ensures each root path has an ID and metadata entry. |
| `facts` | Detector (`store_facts`) | Captures language/build/SCM booleans and tool list. |
| `snapshots` | Snapshotter | Stores tree hash, file/byte counts, timestamps. |
| `events` | Planner & task builder | Records decisions (“SKIP”, “BUILD”, “Build failed”). |
| `commands` | Task builder | History of executed build commands, exit codes, durations. |

This data powers assistant explanations and future analytics.

---

## 9. Extensibility Ideas

- **Configurable ignore patterns** – Allow users to add glob patterns so large generated directories do not
  influence language detection or hashing time.
- **Additional build systems** – Add planners for Gradle, Bazel, SwiftPM, etc.
- **Test orchestration** – Extend `Plan` to cover testing/linting with follow-up recommendations.
- **Heuristic plugins** – Load JSON rule packs (see `rules/`) to adjust planning without recompiling.
- **Caching** – Store per-directory hashes to avoid rescanning big repos on every run.

---

## 10. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Detector labels project `unknown` | Dominant extensions missing or filtered | Add more extension rules in `detector.cpp` or implement configurable patterns. |
| Snapshot takes a long time | Large repo or deep dependency folders | Trim ignore list, run from project root, or plan caching improvements. |
| Planner returns `UNKNOWN` | Build system detection failed | Verify detector found the build file; extend planner if necessary. |
| Facts not visible in assistant | `store_facts` not called | Ensure modules call `ensure_project` followed by `store_facts`. |

Brain keeps Nazg situationally aware. Strengthening detector heuristics, streamlining snapshots, and enriching
planner decisions directly improve the quality of the assistant’s guidance.

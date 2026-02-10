# `nazg status`

`nazg status` gives a quick, non-interactive snapshot of the current workspace. It’s the same context the
assistant would show, distilled into plain text for terminals, automation, and remote sessions.

---

## 1. Purpose

- **Fast context** – Summarise project language, build system, SCM, and git state without launching assistant
  mode.
- **Script-friendly** – Produce deterministic output suitable for CI logs or shell scripts.
- **Guidance** – Highlight divergence, untracked work, or missing git repositories so developers know the next
  step.

---

## 2. Usage

```
nazg status
```

Sample output:
```
📦 Nazg Project Status

Directory : /home/user/projects/nazg
Language  : cpp
Build sys : cmake
SCM       : git
Tools     : cmake, git

Git branch: feature/bot-framework
Upstream  : origin/feature/bot-framework
Divergence: 2 ahead, 1 behind
Changes   : 3 modified, 0 staged, 1 untracked

Tip: Run 'nazg' for interactive assistance.
```

---

## 3. Data Sources

- **Brain detector** – Provides language, build system, SCM, and tool list (`brain::Detector`).
- **Git client** – Supplies branch name, upstream, ahead/behind, and change counts (`git::Client::status`).
- **Directive context** – Delivers logger, Nexus store, and verbose flag if additional detail is needed.

`status` is implemented in `modules/engine/src/runtime.cpp` as part of engine command registration.

---

## 4. Integration Points

- Assistant mode includes a “View project status summary” action that dispatches this command.
- Other modules can reuse the same detector/client calls when presenting contextual cards.
- Plugins can expose their own status fields by extending the command or adding follow-up commands (e.g.,
  `nazg bot status`).

---

## 5. Extension Ideas

- Append planner recommendations (e.g., “Recommended: run cmake build”) once Task records build outcomes.
- Surface recent bot alerts (“Doctor bot: disk warning on lab-box”).
- Provide colour-coded severity indicators when divergence or untracked files exceed thresholds.
- Offer JSON output for tooling (`nazg status --json`) by reusing detector/client data structures.

---

## 6. Troubleshooting

| Symptom | Cause | Resolution |
|---------|-------|-----------|
| “Not a git repository” message | `.git/` missing | Initialise git (`nazg git-init`) or move into a repo. |
| Language/build reported as `unknown` | Detector could not infer project type | Ensure key files exist (e.g., `CMakeLists.txt`) or add detection heuristics. |
| Divergence fields empty | No upstream configured | Run `git push -u origin <branch>` or set tracking via `nazg git-config`. |
| Output lacks colours | ANSI disabled or minimal terminal | Behaviour is by design; text remains readable. |

`nazg status` keeps developers oriented no matter where they run Nazg. Use it as a lightweight heartbeat while
leaving assistant mode for deeper guidance.

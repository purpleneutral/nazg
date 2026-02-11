# Scaffold Module (`modules/scaffold`)

The scaffold module bootstraps new projects with sensible defaults. It generates language-specific templates,
optional tooling setup, and integrates with the assistant so an empty directory can become a working project in
one command.

---

## 1. What It Provides

- **Project skeletons** for C, C++, and Python (extensible to other languages).
- **Support files** such as `CMakeLists.txt`, `README.md`, `.gitignore`, and starter source files.
- **Optional tooling**: Python virtual environments, direnv hooks, and tailored `.gitignore` entries.
- **Assistant hooks**: When Nazg detects an empty or near-empty directory, it offers to scaffold a project for
  you.

---

## 2. Core Types

| Type | Description |
|------|-------------|
| `scaffold::Language` | Enum of supported templates (`C`, `CPP`, `PYTHON`). |
| `scaffold::ScaffoldSpec` | User-supplied parameters (language, project name, root path, in-place flag, direnv/venv toggles). |
| `scaffold::ScaffoldResult` | Outcome struct indicating success, created path, and message. |

Implementation lives under `modules/scaffold/src/` with templates in `modules/scaffold/include/scaffold/templates.hpp`.

---

## 3. Execution Flow

```cpp
scaffold::ScaffoldSpec spec;
spec.lang = scaffold::Language::CPP;
spec.name = "awesome-app";
spec.root = ".";
spec.in_place = false;
spec.use_direnv = false;
spec.create_venv = false;

auto result = scaffold::scaffold_project(spec, logger);
```

Steps performed by `scaffold_project`:
1. Resolve target path based on `root`, `name`, and `in_place`.
2. Validate the destination (create directory or ensure existing directory is safe for in-place scaffolding).
3. Write template files appropriate for the selected language (`src/main.cpp`, `CMakeLists.txt`, `.gitignore`, `README.md`, etc.).
4. For Python projects, optionally run `python3 -m venv .venv` and create `.envrc` when requested.
5. Log progress via Blackbox and return `ScaffoldResult` summarising success or failure.

Failures (e.g., directory already exists or write error) are reported through both the return value and the
logger.

---

## 4. Templates Snapshot

- **C / C++**: Minimal CMake project, main source file, language-appropriate `.gitignore`, starter README.
- **Python**: `src/main.py`, `requirements.txt`, `.gitignore`, README, optional virtual environment and direnv
  hook.

Templates can be customised by editing the helper functions in `templates.hpp`.

---

## 5. Assistant & CLI Integration

- CLI commands registered via `scaffold::register_commands` expose verbs such as `nazg init cpp <name>`.
- Assistant mode checks for empty directories and adds the action “🚀 Create a new project (C++/Python/C)”. When
  selected, it prompts for details and invokes the scaffold command under the hood.

---

## 6. Extending to New Languages

1. Add a new enum entry to `scaffold::Language`.
2. Provide template generators (source files, build configuration, support files).
3. Update `scaffold_project` switch statements to write the new templates.
4. Adjust assistant copy and CLI help to advertise the new language.
5. Document the template here so users know what to expect.

---

## 7. Safety Considerations

- In-place scaffolding warns (logs) when the directory contains files but does not delete anything.
- File writes go through helper functions that create parent directories and propagate failures.
- Virtual environment creation errors are non-fatal; the scaffold succeeds without a venv when installation is
  not possible.

---

## 8. Future Ideas

- Configurable template overlays (organisation-specific licences, CI workflows, code style configs).
- Support for additional languages (Rust, Go, TypeScript, Swift).
- Integration with task module to run an initial build/test immediately after scaffolding.
- Prompt-driven options (choose licence, enable docs folder, set default namespace) through assistant menus.

---

## 9. Troubleshooting

| Issue | Likely Cause | Fix |
|-------|--------------|-----|
| “Directory already exists” | Target path conflicts with an existing folder when `in_place=false` | Choose another project name or remove the directory. |
| Virtual environment creation failed | Missing `python3` or venv module | Install Python 3 with venv support; scaffold output remains usable without the venv. |
| Files missing after scaffold | Template not updated for the selected language | Check `templates.hpp` and ensure language case handles every file. |
| Assistant did not offer scaffolding | Directory not empty or detection skipped | Clear stray files or run `nazg init` manually. |

Scaffold is the first impression many users have of Nazg—keep templates polished and aligned with best
practices so new projects start on the right foot.

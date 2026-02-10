# Git Module (`modules/git`)

Nazg's Git module wraps command-line Git operations in a safe, assistant-friendly API. It exposes a rich client
for other modules, registers CLI commands, and feeds assistant prompts with branch and divergence data.

---

## Quick Reference

**Git Client Usage:**
```cpp
git::Client client(repo_path, log);
if (!client.is_repo()) {
  client.init("main");
}

// Get status
auto status = client.status();
std::cout << "Branch: " << status.branch << "\n";
std::cout << "Modified: " << status.modified << "\n";

// Stage and commit
client.add_all();
client.commit("feat: add new feature");
client.push("origin", "main");

// Config
client.ensure_identity("Name", "email@example.com");
auto name = client.get_config_value("user.name");
```

**CLI Commands:**
- `nazg git-init [branch]` - Initialize git repo
- `nazg git-status` - Detailed status
- `nazg git-config` - Configure identity
- `nazg git-commit <message>` - Quick commit

**Status Fields:**
`status.branch`, `status.upstream`, `status.ahead`, `status.behind`, `status.modified`, `status.staged`, `status.untracked`

---

## 1. Capabilities

- **Client abstraction** – `git::Client` executes git commands with proper quoting, logging, and status parsing.
- **Interactive commands** – Registers `nazg git-*` verbs (init, status, config, commit helpers, server checks).
- **Assistant data** – Produces the branch/upstream/divergence information displayed in assistant mode and
  `nazg status` output.
- **Repository hygiene** – `git::Maintenance` can scaffold `.gitignore`, `.gitattributes`, and ensure initial
  commits exist.
- **Server utilities** – Bare repository helpers and cgit integration lay the groundwork for remote repo
  management.

---

## 2. `git::Client`

```cpp
git::Client client(repo_path, log);
if (!client.is_repo()) {
  client.init("main");
}
auto status = client.status();
```

Key methods:

| Method | Description |
|--------|-------------|
| `bool is_repo()` | Detects whether the target path is inside a git repository. |
| `bool init(branch)` | Initialise a repository with optional default branch. Ensures identity is configured. |
| `Status status()` | Returns branch name, upstream, ahead/behind counts, and change tallies (porcelain v2). |
| `bool add_all()` / `bool add(files)` | Stage changes safely. |
| `bool commit(message)` | Create a commit; message is shell-quoted automatically. |
| `bool push(remote, branch)` | Push changes (explicit remote or defaults to `origin`). |
| `std::optional<std::string> get_config_value(key, global)` | Read git config entries. |
| `bool set_config_value(key, value, global)` | Write config entries (local or global). |
| `bool ensure_identity(name, email)` | Ensure global name/email exist before committing. |
| `std::vector<std::pair<std::string,std::string>> list_remotes()` | List configured remotes with URLs. |

All commands are assembled using `nazg::system::shell_quote`, preventing injection when user-controlled values
are present.

`git::Status` contains:
- `bool in_repo`
- `bool has_commits`
- `std::string branch`
- `std::optional<std::string> upstream`
- `int ahead`, `behind`
- `int modified`, `staged`, `untracked`
- `std::optional<std::string> origin_url`

---

## 3. CLI Commands

`modules/git/src/commands.cpp` registers numerous commands via `directive::registry`, for example:

| Command | Purpose |
|---------|---------|
| `nazg git-init` | Initialise a repository, create a `.gitignore`, ensure user identity, optional first commit. |
| `nazg git-status` | Print branch, upstream, divergence, and change counts in a friendly format. |
| `nazg git-config` | Inspect or modify git config values (local/global). |
| `nazg git-commit` | Stage all changes and create a commit with a provided message. |
| `nazg git-remote-add` | Safely add a remote with quoting/validation. |
| `nazg git-server-status` | Summarise repositories hosted on a configured cgit instance. |

Each handler receives the shared directive context, giving access to the logger, Nexus store, and prompt module
for confirmations.

---

## 4. Maintenance Helpers

`git::Maintenance` offers utilities for keeping repositories tidy:
- `generate_gitignore(language, repo_path)` – Emit a `.gitignore` tuned to the detected language (C/C++, Python,
  Rust, etc.).
- `ensure_initial_commit(repo_path)` – Perform a first commit if the repo is empty.
- `generate_gitattributes(language, repo_path)` – Optionally scaffold `.gitattributes` for consistent diffs.

These helpers are used by `git-init` and can be reused by other modules when provisioning environments.

---

## 5. Assistant Integration

- Assistant mode queries `git::Client::status()` to populate branch/upstream/divergence facts.
- Git actions (“Commit changes”, “View git status”, “Configure git settings”) are added to the assistant menu
  when a repository is detected.
- Wizards such as `git-init` use the prompt module to confirm actions and display what will be created.

---

## 6. Safety Considerations

- Always construct commands via `shell_quote` to avoid injection (commit messages, branch names, remote URLs).
- Log operations and errors through the shared logger; sensitive data (e.g., access tokens) should be masked if
  future features expose them.
- For destructive operations (force push, remote removal), require explicit confirmation before executing.

---

## 7. Future Work

- Automatic branch suggestions and ahead/behind hints (“pull before pushing”).
- Credential helper integration with OS keychains.
- Rich diff presentation through the prompt module.
- Git provider APIs (GitHub/GitLab) for reviews, issues, and merge request summaries.
- Background watcher that notifies the assistant when repository state changes.

---

## 8. Troubleshooting

| Issue | Cause | Fix |
|-------|-------|----|
| `git::Client::status` throws | Repository path invalid or inaccessible | Ensure `repo_path` points to an existing repo and permissions allow running git. |
| Divergence numbers missing | Upstream not configured | Use `git push -u origin <branch>` or `nazg git-config` to set tracking. |
| Commands fail with quoting errors | Command constructed manually | Use `git::Client` and `shell_quote` helpers instead of raw `system` calls. |
| `.gitignore` not generated | Maintenance helper not invoked | Call `generate_gitignore` during scaffolding or repo initialisation. |

Nazg’s Git module keeps version control insights accessible and safe. Reuse its client and helpers whenever a
feature needs to interact with Git.

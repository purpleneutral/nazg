# Prompt Module Design

## Overview

The **prompt** module provides context-aware, rich interactive prompts and progress indicators for nazg. It replaces bare `std::cin` calls with intelligent, styled prompts that show the user what's happening.

---

## Design Principles

1. **Context-Aware**: Auto-detect project state, command, language, etc.
2. **Progressive Enhancement**: Works in basic terminals, beautiful in modern ones
3. **Non-Intrusive**: Clean, scannable, not overwhelming
4. **Consistent**: All prompts follow the same visual language
5. **Scriptable**: Gracefully handles non-interactive environments

---

## Visual Structure

### Standard Mode (Default)

```
┌─ nazg • git-init ─────────────────────────
│ Project: ~/projects/myproject (C++)
├───────────────────────────────────────────
│ Initialize git repository?
│   • Create .git/
│   • Generate .gitignore for C++
│   • Initial commit
└─> (Y/n)
```

### Minimal Mode

```
nazg [git-init] • Initialize git repository?
  • Create .git/
  • Generate .gitignore
  • Initial commit
(Y/n)
```

### Verbose Mode

```
┌─ nazg • git-init ──────────────────── [2025-10-02 16:30]
│
│ Project:   ~/projects/myproject
│ Language:  C++
│ Status:    not a git repository
│
├─ Action ───────────────────────────────────────────────
│
│ Initialize git repository?
│
│ This will:
│   • Create .git/ directory
│   • Generate .gitignore for C++
│   • Set up git identity (user: director)
│   • Create initial commit
│
└─> (Y/n)
```

---

## API Design

### Main Prompt Class

```cpp
namespace nazg::prompt {

enum class Style {
  MINIMAL,   // Compact, single-line where possible
  STANDARD,  // Box with essential context (default)
  VERBOSE    // Full context panel with timestamps
};

enum class Icon {
  SUCCESS,   // ✓
  ERROR,     // ✗
  WARNING,   // ⚠
  INFO,      // ℹ
  PROGRESS,  // ⏳
  ARROW      // →
};

class Prompt {
public:
  explicit Prompt(nazg::blackbox::logger* log = nullptr);

  // ── Context (auto-populated from directive::context if available) ──
  Prompt& title(const std::string& command_name);
  Prompt& project(const std::string& name, const std::string& path);
  Prompt& fact(const std::string& key, const std::string& value);
  Prompt& status(const std::string& message);
  Prompt& timestamp(bool show = true);

  // ── Question/Action ──
  Prompt& question(const std::string& text);
  Prompt& action(const std::string& description);
  Prompt& warning(const std::string& text);
  Prompt& info(const std::string& text);
  Prompt& detail(const std::string& text);  // Gray/dim text for extra info

  // ── Display & Input ──
  bool confirm(bool default_yes = true);
  int choice(const std::vector<std::string>& options, int default_choice = 0);
  std::string input(const std::string& placeholder = "");

  // ── Configuration ──
  Prompt& style(Style s);
  Prompt& colors(bool enabled);  // Auto-detected if not set

private:
  // Auto-detect terminal capabilities
  bool supports_color() const;
  bool is_interactive() const;

  // Render helpers
  void render_header();
  void render_body();
  void render_footer();
};

// ── Quick Helpers ──
bool confirm(const std::string& question, bool default_yes = true);
int choose(const std::string& question,
           const std::vector<std::string>& options,
           int default_choice = 0);
std::string ask(const std::string& question,
                const std::string& placeholder = "");

// ── Progress Indicators ──
class Spinner {
public:
  explicit Spinner(const std::string& message);
  ~Spinner();

  void start();
  void stop(const std::string& final_message = "");
  void update(const std::string& message);

private:
  // Thread-safe spinner animation
  std::thread spinner_thread_;
  std::atomic<bool> running_{false};
  std::string message_;
};

class ProgressBar {
public:
  explicit ProgressBar(size_t total, const std::string& label = "");

  void update(size_t current);
  void finish();

private:
  size_t total_;
  size_t current_{0};
  std::string label_;
};

class Workflow {
public:
  explicit Workflow(const std::string& title);

  void step(const std::string& description);
  void run(std::function<bool(int)> execute_step);

private:
  std::string title_;
  std::vector<std::string> steps_;
};

} // namespace nazg::prompt
```

---

## Usage Examples

### Example 1: Simple Confirmation

```cpp
#include "prompt/prompt.hpp"

// Quick helper
if (prompt::confirm("Initialize git repository?")) {
  git::Client client(cwd, log);
  client.init("main");
}
```

### Example 2: Rich Git Init Prompt

```cpp
prompt::Prompt p;
p.title("git-init")
 .project("myproject", "~/projects/myproject")
 .fact("Language", "C++")
 .fact("Build System", "CMake")
 .status("not a git repository")
 .question("Initialize git repository?")
 .action("Create .git/ directory")
 .action("Generate .gitignore for C++")
 .action("Set up git identity")
 .action("Create initial commit")
 .info("You can configure git later with 'git config'");

if (p.confirm()) {
  // do init
}
```

### Example 3: Server Installation

```cpp
prompt::Prompt p;
p.title("git-server-install")
 .fact("Server", "10.0.0.4")
 .fact("Type", "cgit")
 .status("not installed")
 .question("Install cgit on 10.0.0.4?")
 .action("SSH to server and verify connectivity")
 .action("Install cgit, nginx, fcgiwrap via package manager")
 .action("Configure nginx for cgit web UI")
 .action("Create /srv/git repository directory")
 .action("Set up permissions for git user")
 .warning("Requires sudo access on remote server")
 .detail("Estimated time: 2-5 minutes depending on network");

if (p.confirm()) {
  server->install();
}
```

### Example 4: Multiple Choice

```cpp
prompt::Prompt p;
p.title("scaffold")
 .question("Choose project language:");

int choice = p.choice({
  "C (with CMake)",
  "C++ (with CMake)",
  "Python (with venv + direnv)",
  "Rust (with Cargo)",
  "Go (with modules)"
});

switch (choice) {
  case 0: scaffold_c(); break;
  case 1: scaffold_cpp(); break;
  // ...
}
```

### Example 5: Text Input

```cpp
prompt::Prompt p;
p.title("git-commit")
 .fact("Modified files", "3")
 .fact("Staged files", "3")
 .question("Enter commit message:");

std::string msg = p.input("Brief description of changes");
if (!msg.empty()) {
  client.commit(msg);
}
```

### Example 6: Progress Spinner

```cpp
prompt::Spinner spin("Installing packages on 10.0.0.4");
spin.start();

bool ok = ssh_exec("sudo apt-get install -y cgit nginx");

if (ok) {
  spin.stop("✓ Packages installed");
} else {
  spin.stop("✗ Installation failed");
}
```

### Example 7: Progress Bar

```cpp
std::vector<std::string> repos = {"nazg.git", "website.git", "tools.git"};

prompt::ProgressBar progress(repos.size(), "Syncing repositories");

for (size_t i = 0; i < repos.size(); ++i) {
  sync_repo(repos[i]);
  progress.update(i + 1);
}

progress.finish();
```

### Example 8: Multi-Step Workflow

```cpp
prompt::Workflow wf("Git Server Setup");
wf.step("Test SSH connection");
wf.step("Install packages");
wf.step("Configure nginx");
wf.step("Create repo directory");
wf.step("Set permissions");

wf.run([&](int step) {
  switch (step) {
    case 0: return server->test_connection();
    case 1: return server->install_packages();
    case 2: return server->configure_nginx();
    case 3: return server->create_repo_dir();
    case 4: return server->set_permissions();
  }
  return false;
});
```

---

## Color & Icons

### Color Scheme

| Element         | Color       | Fallback  |
|-----------------|-------------|-----------|
| Title/Header    | Cyan/Bold   | Bold      |
| Facts/Keys      | Blue        | Normal    |
| Success         | Green       | Normal    |
| Error           | Red         | Normal    |
| Warning         | Yellow      | Normal    |
| Info            | Cyan        | Normal    |
| Details/Dim     | Gray        | Normal    |
| Prompt          | Bold        | Bold      |

### Icons

| Icon      | Unicode | ASCII Fallback |
|-----------|---------|----------------|
| Success   | ✓       | [OK]           |
| Error     | ✗       | [FAIL]         |
| Warning   | ⚠       | [WARN]         |
| Info      | ℹ       | [INFO]         |
| Progress  | ⏳      | [...]          |
| Arrow     | →       | >              |
| Bullet    | •       | *              |

---

## Terminal Detection

### Color Support

```cpp
bool Prompt::supports_color() const {
  // Check environment variables
  const char* term = std::getenv("TERM");
  if (!term) return false;

  std::string term_str(term);
  if (term_str == "dumb") return false;

  // Check for color support
  const char* colorterm = std::getenv("COLORTERM");
  if (colorterm) return true;

  // Check common color-capable terminals
  return term_str.find("color") != std::string::npos ||
         term_str.find("xterm") != std::string::npos ||
         term_str.find("screen") != std::string::npos;
}
```

### Interactive Mode

```cpp
bool Prompt::is_interactive() const {
  return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}
```

---

## Non-Interactive Mode

When `stdin` is not a TTY (e.g., running in a script or CI):

### Behavior Options

1. **Use defaults** (safest for automation)
   ```cpp
   if (!is_interactive()) {
     if (log_) log_->info("Prompt", "Non-interactive: using default '"
                          + (default_yes ? "yes" : "no") + "'");
     return default_yes;
   }
   ```

2. **Fail with error** (explicit)
   ```cpp
   if (!is_interactive()) {
     throw std::runtime_error("Cannot prompt in non-interactive mode. "
                             "Use --yes flag for automation.");
   }
   ```

3. **Check `--yes` flag** (from directive::context)
   ```cpp
   if (ctx.force_yes || !is_interactive()) {
     return true;
   }
   ```

### Recommended Approach

Combine all three:
- Check for `--yes` / `--no` / `--assume-yes` CLI flags
- If not set and non-interactive, use default
- Log what decision was made
- Allow `NAZG_NONINTERACTIVE=1` env var to disable all prompts

```cpp
bool Prompt::confirm(bool default_yes) {
  // 1. Check CLI flags (from context)
  if (ctx_ && ctx_->force_yes) return true;
  if (ctx_ && ctx_->force_no) return false;

  // 2. Check environment
  if (std::getenv("NAZG_NONINTERACTIVE")) {
    if (log_) log_->info("Prompt", "Non-interactive mode: using default");
    return default_yes;
  }

  // 3. Check if TTY
  if (!is_interactive()) {
    if (log_) log_->warn("Prompt", "stdin not a TTY: using default");
    return default_yes;
  }

  // 4. Actually prompt the user
  render_header();
  render_body();
  render_footer();

  return read_confirmation(default_yes);
}
```

---

## Configuration

### Config File (`~/.config/nazg/config.toml`)

```toml
[prompt]
style = "standard"        # minimal, standard, verbose
colors = true             # auto-detect if not set
icons = true              # use unicode icons
show_timestamp = false    # show timestamps in verbose mode
show_project_context = true
box_style = "rounded"     # rounded, square, ascii

# Non-interactive behavior
noninteractive_mode = "default"  # default, fail, always_yes
```

### Environment Variables

```bash
NAZG_PROMPT_STYLE=minimal   # Override style
NAZG_NO_COLOR=1             # Disable colors
NAZG_NONINTERACTIVE=1       # Use defaults, no prompts
FORCE_COLOR=1               # Force color support
NO_COLOR=1                  # Standard: disable colors
```

---

## Integration with Existing Modules

### Auto-Context from directive::context

```cpp
// In command implementation
int cmd_git_init(const directive::command_context& cctx,
                 const directive::context& ectx) {

  prompt::Prompt p(ectx.log);  // Pass logger

  // Auto-populate from context
  if (ectx.store) {
    auto project = ectx.store->get_current_project();
    p.project(project.name, project.path);
    p.fact("Language", project.language);
  }

  // Command-specific
  p.title("git-init")
   .question("Initialize git repository?")
   .action("Create .git/")
   .action("Generate .gitignore");

  if (p.confirm()) {
    // do init
  }
}
```

---

## Module Structure

```
modules/prompt/
├── include/prompt/
│   ├── prompt.hpp         # Main Prompt class
│   ├── spinner.hpp        # Progress indicators
│   ├── colors.hpp         # Color codes & terminal detection
│   ├── icons.hpp          # Icon definitions
│   └── render.hpp         # Rendering utilities
├── src/
│   ├── prompt.cpp
│   ├── spinner.cpp
│   ├── colors.cpp
│   ├── render.cpp
│   └── terminal.cpp       # Terminal capability detection
└── README.md
```

---

## Implementation Phases

### Phase 1: Core Prompt (Week 1)
- [x] Document design
- [ ] Basic Prompt class
- [ ] Terminal detection (color, TTY)
- [ ] confirm() / choice() / input()
- [ ] Standard style rendering
- [ ] Non-interactive mode handling

### Phase 2: Visual Polish (Week 1-2)
- [ ] Color support
- [ ] Icon support
- [ ] Minimal & Verbose styles
- [ ] Box drawing helpers
- [ ] Config file support

### Phase 3: Progress Indicators (Week 2)
- [ ] Spinner class (threaded animation)
- [ ] ProgressBar class
- [ ] Workflow class (multi-step)
- [ ] Integration with task execution

### Phase 4: Integration (Week 2-3)
- [ ] Auto-context detection
- [ ] Integrate into git commands
- [ ] Integrate into scaffold commands
- [ ] Integrate into update/build commands
- [ ] Add `--yes` / `--no` CLI flags

---

## Examples of Real-World Usage

### Git Init Flow

```
┌─ nazg • git-init ─────────────────────────
│ Project: myproject (C++)
│ Path:    ~/projects/myproject
├───────────────────────────────────────────
│ Initialize git repository?
│   • Create .git/ directory
│   • Generate .gitignore for C++
│   • Create initial commit
└─> (Y/n) y

⏳ Initializing repository...
✓ Created .git/
✓ Generated .gitignore
✓ Initial commit created

Repository initialized successfully!
```

### Server Installation Flow

```
┌─ nazg • git-server-install ───────────────
│ Server:  10.0.0.4 (cgit)
│ Status:  not installed
├───────────────────────────────────────────
│ Install cgit on 10.0.0.4?
│   • SSH to server
│   • Install cgit, nginx, fcgiwrap
│   • Configure nginx
│   • Create /srv/git directory
│   ⚠ Requires sudo access
└─> (Y/n) y

Step 1/5: Test SSH connection...       ✓
Step 2/5: Install packages...          ⏳
```

---

## Summary

The prompt module transforms nazg from a command-line tool into an **intelligent assistant** that:
- Shows context before asking questions
- Explains what actions will be taken
- Handles automation gracefully
- Provides visual feedback during long operations
- Maintains a consistent, professional appearance

This makes nazg feel more like a **conversational tool** rather than just another CLI utility.

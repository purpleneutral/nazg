# Nazg TUI Design

## Overview

This document outlines the design for a terminal user interface (TUI) for nazg, invoked with `nazg` or `nazg tui`. The TUI provides:

1. **Component-based menus**: Declarative composition of UI elements (like React/Flutter)
2. **Terminal multiplexing**: tmux-like functionality with windows, panes, and splits
3. **Modal editing**: Neovim-like modes (NORMAL, INSERT, PREFIX, VISUAL, COMMAND)
4. **Modular architecture**: External modules can register menus and components

**See also**: `docs/tui_component_architecture.md` for detailed component system design

---

## Architecture

### Manager-Based Design (Current Implementation)

The TUI system uses a **manager-based architecture** where each major concern is handled by a dedicated manager:

```
┌─────────────────────────────────────────────────────────────────┐
│                         TUIApp                                  │
│                      (Entry Point)                              │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│                       TUIContext                                │
│              (Central API & State Container)                    │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐         │
│  │ KeyManager   │  │ ModeManager  │  │CommandManager│         │
│  │              │  │              │  │              │         │
│  │ - Bindings   │  │ - Current    │  │ - Registry   │         │
│  │ - Lookup     │  │ - Transitions│  │ - Execution  │         │
│  │ - Registration│  │ - Prefix     │  │ - Builtins   │         │
│  └──────────────┘  └──────────────┘  └──────────────┘         │
│                                                                 │
│  ┌─────────────────────────────────────────────────┐           │
│  │           Window Management                     │           │
│  │  - Create/Close windows                         │           │
│  │  - Focus/Navigate windows                       │           │
│  │  - Active window tracking                       │           │
│  └─────────────────────────────────────────────────┘           │
└─────────────────────────────────────────────────────────────────┘
         │                  │                  │
         ▼                  ▼                  ▼
  ┌──────────┐      ┌──────────┐      ┌──────────┐
  │ Window   │      │StatusBar │      │  Theme   │
  │  - Panes │      │          │      │          │
  │  - Layout│      │          │      │          │
  └──────────┘      └──────────┘      └──────────┘
       │
       ▼
  ┌──────────┐
  │   Pane   │
  │ - Terminal│
  │ - PTY    │
  └──────────┘
```

### Manager Responsibilities

1. **KeyManager**:
   - Manages all keybindings
   - Provides registration API for external modules
   - Lookup keybindings by event and mode
   - Default tmux-like bindings
   - **NEW:** Per-component keybinding registration

2. **ModeManager**:
   - Tracks current mode (NORMAL, INSERT, PREFIX, VISUAL, COMMAND)
   - Handles mode transitions
   - Prefix key activation and timeout
   - Mode change callbacks

3. **CommandManager**:
   - Registry of all commands
   - Command execution with arguments
   - Built-in commands (split, navigate, window management, etc.)
   - Extensible for external modules

4. **ComponentRegistry**:
   - Registry of all TUI components (menus, screens, custom views)
   - Dynamic component loading via `:load` command
   - Component lifecycle management
   - Focus tracking and event routing

5. **TUIContext**:
   - Central access point for all managers
   - Window lifecycle management
   - Status and logging
   - Public API for external modules

6. **Window/Pane**:
   - Terminal multiplexing
   - Layout management (binary tree)
   - PTY/shell integration
   - Rendering

### Key Features

1. **tmux-like multiplexing**: Windows, panes, tabs
2. **Prefix key system**: Configurable prefix (default `C-b`) with visual indicator
3. **Modal system**: Normal, Insert, Visual, Command modes (vim-style)
4. **Customizable keybindings**: Full key remapping support
5. **Session persistence**: Save and restore sessions
6. **Status bar**: Displays windows, mode, prefix indicator, time, etc.
7. **External module API**: Other modules can register keybindings and commands

### External Module Integration

The TUI module provides a **public API** (`tui/tui_api.hpp`) that allows other nazg modules (like `git`, `task`, `workspace`, etc.) to extend TUI functionality through a **component-based architecture**.

#### Component-Based Architecture

All TUI components (menus, screens, views) implement a common `ComponentBase` interface:

```cpp
namespace nazg::tui {

class ComponentBase {
public:
  virtual ~ComponentBase() = default;

  // Lifecycle
  virtual void on_activate() {}
  virtual void on_deactivate() {}

  // Focus management
  virtual void on_focus() {}
  virtual void on_blur() {}
  virtual bool is_focused() const { return focused_; }

  // Event handling (return true if event was handled)
  virtual bool handle_event(const ftxui::Event& event) = 0;

  // Rendering
  virtual ftxui::Element render(int width, int height, const Theme& theme) = 0;

  // Component identification
  virtual std::string id() const = 0;
  virtual std::string name() const = 0;

protected:
  bool focused_ = false;
};

} // namespace nazg::tui
```

#### Example: Git Module Integration

```cpp
#include "tui/tui_api.hpp"
#include "tui/components/menu_list.hpp"

namespace nazg::git {

class GitStatusMenu : public tui::ComponentBase {
public:
  GitStatusMenu() {
    // Initialize menu items
    items_ = {"View Status", "Commit Changes", "Push", "Pull", "History"};
  }

  bool handle_event(const ftxui::Event& event) override {
    // j/k navigation handled by MenuList
    if (event.character() == "j") { selected_++; return true; }
    if (event.character() == "k") { selected_--; return true; }
    if (event == ftxui::Event::Return) {
      execute_selected();
      return true;
    }
    return false;
  }

  ftxui::Element render(int width, int height, const Theme& theme) override {
    return menu_list_.render(items_, selected_, width, height, theme);
  }

  std::string id() const override { return "GitStatus"; }
  std::string name() const override { return "Git Status"; }

private:
  std::vector<std::string> items_;
  int selected_ = 0;
  tui::MenuList menu_list_;
};

// Register with TUI at module initialization
void init_tui(tui::TUIContext& ctx) {
  // Register component
  ctx.components().register_component(
    "GitStatus",
    std::make_unique<GitStatusMenu>()
  );

  // Register command to load the component
  ctx.commands().register_command(
    "git-status",
    "Show git status menu",
    [](auto& ctx, auto& args) {
      return ctx.components().load("GitStatus");
    }
  );

  // Bind Ctrl-B + g to load git menu
  ctx.keys().bind({"g", "git-status", "Git status", tui::Mode::PREFIX, true});
}

} // namespace nazg::git
```

#### Module Capabilities

This allows modules to:
- **Register custom components** (menus, screens, views) that integrate seamlessly
- **Define component-specific keybindings** that activate when component is focused
- **Register commands** that can be invoked via keybindings or command mode
- **Load components dynamically** via `:load <component>` command
- **Access TUI state** (active window, status, theme, etc.)
- **Create interactive menus** using built-in MenuList component
- **Update status bar** with module-specific information

---

## TUI Startup Workflow

The TUI follows a modular startup workflow that allows for extensibility:

### Default Startup Flow

```
┌─────────────────────────────────────────────────────────────┐
│ 1. User runs "nazg"                                         │
│    → TUIApp initializes                                     │
│    → Managers initialize (Key, Mode, Command, Component)   │
│    → Built-in components register                           │
│    → External modules register their components             │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ 2. WelcomeScreen component loads by default                │
│    → Shows nazg logo and version                            │
│    → Displays available commands and keybindings            │
│    → Prompts to type ":load MainMenu" or ":help"           │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ 3. User types ":load MainMenu"                              │
│    → CommandBar parses and executes command                 │
│    → ComponentRegistry loads MainMenu component             │
│    → MainMenu activates and receives focus                  │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ 4. MainMenu displays using MenuList component              │
│    → Shows available nazg modules (Git, Tasks, Workspace)   │
│    → User navigates with j/k, selects with Enter           │
│    → Selected module's component loads                      │
└─────────────────────────────────────────────────────────────┘
```

### Component Loading via `:load` Command

Users can load any registered component using the command bar:

- `:load MainMenu` - Load main menu
- `:load GitStatus` - Load git module's status menu
- `:load TaskList` - Load task module's task list
- `:load <component-id>` - Load any registered component

Components can also load other components programmatically:

```cpp
// From within a component
ctx.components().load("GitStatus");
```

### Component vs Window/Pane Architecture

The TUI supports **two distinct modes of operation**:

1. **Component Mode** (new):
   - Full-screen components (menus, dashboards, screens)
   - Single component active at a time (or component stack)
   - Focus managed by ComponentRegistry
   - Keybindings scoped to active component

2. **Terminal Mode** (existing):
   - Window/Pane multiplexing (tmux-like)
   - Multiple panes visible with split layouts
   - Focus managed by Window
   - Terminal I/O and PTY management

Users can switch between modes:
- `:terminal` - Enter terminal mode (create window/panes)
- `:load <component>` - Load a component (leaves terminal mode)
- `Ctrl-B + t` - Toggle between last terminal window and component

Both modes coexist: you can have terminals running in the background while viewing a menu.

---

## Module Structure

```
modules/tui/
├── include/tui/
│   ├── tui.hpp              # Main TUI application
│   ├── terminal.hpp         # PTY/terminal management
│   ├── window.hpp           # Window container
│   ├── pane.hpp             # Individual pane (runs process)
│   ├── layout.hpp           # Layout engine (splits)
│   ├── compositor.hpp       # Rendering engine
│   ├── input.hpp            # Input handling & key events
│   ├── mode.hpp             # Modal system (normal/insert/visual/command)
│   ├── keymap.hpp           # Key binding system
│   ├── statusbar.hpp        # Status bar with prefix indicator
│   ├── tabbar.hpp           # Tab management
│   ├── config.hpp           # TUI-specific config
│   └── session.hpp          # Session persistence
└── src/
    ├── tui.cpp
    ├── terminal.cpp          # PTY creation/management
    ├── window.cpp
    ├── pane.cpp
    ├── layout.cpp
    ├── compositor.cpp
    ├── input.cpp
    ├── mode.cpp
    ├── keymap.cpp
    ├── statusbar.cpp
    ├── tabbar.cpp
    ├── config.cpp
    └── session.cpp
```

---

## Core Components

### 1. Modal System

```cpp
namespace nazg::tui {

enum class Mode {
  NORMAL,    // Default: navigate, resize, create panes
  INSERT,    // Pass all keys to active pane
  VISUAL,    // Select/copy text from panes
  COMMAND,   // Command line (`:` prefix like vim)
  PREFIX     // After prefix key pressed
};

class ModeManager {
public:
  Mode current() const;
  void enter(Mode m);
  void exit();

  // Mode-specific key handlers
  void handle_normal_key(KeyEvent ke);
  void handle_insert_key(KeyEvent ke);
  void handle_visual_key(KeyEvent ke);
  void handle_command_key(KeyEvent ke);
  void handle_prefix_key(KeyEvent ke);

private:
  Mode mode_ = Mode::NORMAL;
  Mode prev_mode_ = Mode::NORMAL;
  std::chrono::steady_clock::time_point prefix_time_;
  bool prefix_active_ = false;
};

}
```

**Mode Descriptions**:

- **NORMAL**: Navigate between panes, resize, create/destroy panes and windows. Uses vim-like hjkl navigation without prefix.
- **INSERT**: All input passes directly to the active pane's shell. This is the "passthrough" mode.
- **VISUAL**: Select and copy text from panes. Use vim-style visual selection.
- **COMMAND**: Enter `:` commands (like `:split-horizontal`, `:new-window`, etc.)
- **PREFIX**: Activated after pressing the prefix key (default `C-a`). Shows indicator in status bar for 1 second.

---

### 2. Key Binding System

```cpp
namespace nazg::tui {

struct KeyBinding {
  std::string key;              // "C-a c" or "C-a |" etc
  std::string command;          // "split-horizontal"
  std::string description;      // "Split pane horizontally"
  Mode mode = Mode::NORMAL;     // Active in which mode
};

class KeyMap {
public:
  void bind(const KeyBinding& kb);
  void unbind(const std::string& key, Mode mode);

  std::optional<std::string> lookup(const std::string& key, Mode mode);

  // Default keybindings
  void load_defaults();
  void load_from_config(const config::store& cfg);

private:
  std::unordered_map<Mode, std::map<std::string, KeyBinding>> bindings_;
};

}
```

**Default Keybindings**:

#### Prefix Key Bindings (after `C-a`)

| Key      | Command              | Description                    |
|----------|----------------------|--------------------------------|
| `c`      | new-window           | Create new window              |
| `\|`     | split-vertical       | Split pane vertically          |
| `-`      | split-horizontal     | Split pane horizontally        |
| `h`      | select-pane-left     | Navigate to left pane          |
| `j`      | select-pane-down     | Navigate to pane below         |
| `k`      | select-pane-up       | Navigate to pane above         |
| `l`      | select-pane-right    | Navigate to right pane         |
| `H`      | resize-pane-left     | Resize pane left               |
| `J`      | resize-pane-down     | Resize pane down               |
| `K`      | resize-pane-up       | Resize pane up                 |
| `L`      | resize-pane-right    | Resize pane right              |
| `d`      | kill-pane            | Close current pane             |
| `&`      | kill-window          | Close current window           |
| `n`      | next-window          | Switch to next window          |
| `p`      | previous-window      | Switch to previous window      |
| `0-9`    | select-window-N      | Select window by number        |
| `:`      | command-mode         | Enter command mode             |
| `[`      | copy-mode            | Enter visual/copy mode         |
| `z`      | zoom-pane            | Toggle pane zoom (fullscreen)  |
| `?`      | show-keys            | Show key bindings help         |

#### Normal Mode (no prefix)

| Key      | Command              | Description                    |
|----------|----------------------|--------------------------------|
| `Esc`    | enter-normal-mode    | Return to normal mode          |
| `i`      | enter-insert-mode    | Enter insert mode              |
| `v`      | enter-visual-mode    | Enter visual mode              |
| `:`      | enter-command-mode   | Enter command mode             |

---

### 3. Window and Pane Management

```cpp
namespace nazg::tui {

class Pane {
public:
  Pane(const std::string& cmd, const std::vector<std::string>& args);
  ~Pane();

  void resize(int width, int height);
  void send_input(const std::string& data);
  std::string read_output();

  bool is_alive() const;
  pid_t pid() const;

  // Rendering
  void render(Compositor& comp, Rect area);

private:
  int pty_master_;
  int pty_slave_;
  pid_t child_pid_;

  std::string shell_cmd_;
  std::vector<char> scrollback_;
  int scroll_offset_ = 0;
};

enum class SplitDirection {
  HORIZONTAL,
  VERTICAL
};

class Window {
public:
  Window(int id, const std::string& name);

  // Pane management
  Pane* active_pane();
  void split(SplitDirection dir, const std::string& cmd);
  void close_pane(Pane* pane);
  void navigate(Direction dir);  // h/j/k/l
  void resize_pane(Direction dir, int delta);

  // Layout
  void set_layout(Layout layout);  // tiled, main-vertical, etc.
  void reflow(Rect area);

  // Rendering
  void render(Compositor& comp, Rect area);

private:
  int id_;
  std::string name_;
  std::vector<std::unique_ptr<Pane>> panes_;
  Pane* active_pane_ = nullptr;

  LayoutEngine layout_;
};

class Session {
public:
  Session(const std::string& name);

  // Window management
  Window* create_window(const std::string& name = "");
  void close_window(Window* win);
  Window* active_window();
  void next_window();
  void prev_window();
  void select_window(int index);

  // Rendering
  void render(Compositor& comp);

  // Persistence
  void save(const std::string& path);
  static Session load(const std::string& path);

private:
  std::string name_;
  std::vector<std::unique_ptr<Window>> windows_;
  Window* active_window_ = nullptr;
};

}
```

---

### 4. Status Bar Design

```
┌──────────────────────────────────────────────────────────────┐
│ [PREFIX] 0:zsh  1:vim*  2:build  3:logs      nazg | 14:32    │
└──────────────────────────────────────────────────────────────┘
 ^         ^                                    ^       ^
 prefix    windows/tabs                         info    time
 indicator (active marked with *)
```

```cpp
namespace nazg::tui {

class StatusBar {
public:
  void set_prefix_active(bool active);
  void set_mode(Mode mode);
  void set_windows(const std::vector<Window*>& windows, int active_idx);

  void render(Compositor& comp, Rect area);

private:
  bool prefix_active_ = false;
  Mode current_mode_ = Mode::NORMAL;
  std::vector<WindowInfo> windows_;
  int active_window_ = 0;

  // Customizable
  std::string left_format_;   // "[{prefix}] {windows}"
  std::string right_format_;  // "{session} | {time}"
};

}
```

**Status Bar Components**:

- **Left side**: Prefix indicator + window list
- **Right side**: Session name, current mode, time
- **Customizable**: Format strings configured in `~/.config/nazg/tui.toml`

---

### 5. Command System

```cpp
namespace nazg::tui {

class CommandRegistry {
public:
  void register_command(const std::string& name,
                       std::function<void(Session&, const std::vector<std::string>&)> fn);

  void execute(Session& session, const std::string& cmdline);

  // Built-in commands
  void register_builtins();

private:
  std::map<std::string, std::function<void(Session&, const std::vector<std::string>&)>> commands_;
};

}
```

**Built-in Commands** (entered with `:` in command mode):

| Command                  | Description                                   |
|--------------------------|-----------------------------------------------|
| `:split-horizontal [cmd]`| Split pane horizontally, optionally run cmd   |
| `:split-vertical [cmd]`  | Split pane vertically, optionally run cmd     |
| `:new-window [name]`     | Create new window with optional name          |
| `:kill-pane`             | Close current pane                            |
| `:kill-window`           | Close current window                          |
| `:resize-pane +10`       | Resize pane by delta                          |
| `:select-pane N`         | Select pane by number                         |
| `:list-windows`          | Show all windows                              |
| `:rename-window <name>`  | Rename current window                         |
| `:save-session [path]`   | Save session to file                          |
| `:load-session [path]`   | Load session from file                        |
| `:set <option> <value>`  | Set runtime option                            |
| `:theme <name>`          | Change color theme                            |
| `:bind <key> <command>`  | Bind key to command                           |
| `:unbind <key>`          | Remove key binding                            |
| `:help [command]`        | Show help for command                         |
| `:quit`                  | Exit TUI                                      |

---

## Configuration

### Configuration File: `~/.config/nazg/tui.toml`

```toml
[tui]
default_shell = "/bin/zsh"
prefix_key = "C-a"           # Ctrl-a (tmux-style)
prefix_timeout_ms = 1000     # Show indicator for 1s
escape_time_ms = 10          # Escape key delay

[tui.statusbar]
show = true
position = "bottom"          # top, bottom
format_left = "[{prefix}] {windows}"
format_right = "{mode} | {time}"
prefix_style = "bold red"
mode_style = "bold cyan"

[tui.appearance]
theme = "gruvbox"            # gruvbox, nord, dracula, solarized, etc.
pane_border_style = "rounded"  # rounded, square, double
active_pane_border = "cyan"
inactive_pane_border = "gray"
show_pane_titles = true

[tui.behavior]
default_mode = "insert"      # Start in insert mode
mouse_support = true         # Enable mouse for pane selection/resize
copy_command = "xclip -selection clipboard"  # Clipboard integration

[tui.keys]
# Prefix key bindings (after C-a)
"c" = "new-window"
"|" = "split-vertical"
"-" = "split-horizontal"
"h" = "select-pane-left"
"j" = "select-pane-down"
"k" = "select-pane-up"
"l" = "select-pane-right"
"H" = "resize-pane-left"
"J" = "resize-pane-down"
"K" = "resize-pane-up"
"L" = "resize-pane-right"
"d" = "kill-pane"
"&" = "kill-window"
"z" = "zoom-pane"
"[" = "copy-mode"
":" = "command-mode"
"n" = "next-window"
"p" = "previous-window"

# Vim-style bindings in normal mode (no prefix needed)
[tui.keys.normal]
"Esc" = "enter-normal-mode"
"i" = "enter-insert-mode"
"v" = "enter-visual-mode"
":" = "enter-command-mode"

[tui.session]
auto_save = true
save_interval_sec = 60
restore_on_start = true
session_dir = "~/.local/share/nazg/sessions"
```

### Environment Variables

```bash
NAZG_TUI_SHELL="/bin/bash"        # Override default shell
NAZG_TUI_PREFIX="C-b"             # Override prefix key
NAZG_TUI_THEME="nord"             # Override theme
NAZG_NO_COLOR=1                   # Disable colors
```

---

## TUI Library Choice

### Recommended: FTXUI

**Pros**:
- Pure C++17, header-only option
- Modern reactive component API
- Excellent performance
- No ncurses dependency
- Active development
- Clean, simple API

**Cons**:
- Less features than notcurses
- Smaller community

### Alternative: notcurses

**Pros**:
- More feature-rich
- Better multimedia support
- Mature and stable
- Good documentation

**Cons**:
- External dependency
- More complex API
- Steeper learning curve

**Recommendation**: Start with **FTXUI** for simplicity and modern C++ integration. Migrate to notcurses if advanced features are needed.

---

## Technical Implementation Details

### PTY Management

Each pane runs a shell in a pseudo-terminal (PTY):

```cpp
// Create PTY pair
int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
grantpt(master_fd);
unlockpt(master_fd);

// Fork child process
pid_t pid = fork();
if (pid == 0) {
  // Child: attach to slave PTY
  int slave_fd = open(ptsname(master_fd), O_RDWR);
  setsid();
  ioctl(slave_fd, TIOCSCTTY, 0);

  dup2(slave_fd, STDIN_FILENO);
  dup2(slave_fd, STDOUT_FILENO);
  dup2(slave_fd, STDERR_FILENO);

  execl(shell, shell, nullptr);
}
// Parent: keep master_fd for I/O
```

**Key Challenges**:
1. Proper cleanup on pane destruction
2. Handling SIGCHLD for dead shells
3. Terminal size updates (TIOCSWINSZ)
4. Scrollback buffer management

### Input Multiplexing

Use `select()` or `epoll()` to monitor multiple PTY master fds:

```cpp
fd_set read_fds;
FD_ZERO(&read_fds);

for (auto& pane : panes) {
  FD_SET(pane->pty_master(), &read_fds);
}

select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

// Check which panes have data
for (auto& pane : panes) {
  if (FD_ISSET(pane->pty_master(), &read_fds)) {
    pane->read_output();
  }
}
```

### Rendering Pipeline

1. **Input**: Capture keyboard/mouse events
2. **Mode Handling**: Route to appropriate mode handler
3. **State Update**: Update pane/window state
4. **Layout**: Calculate pane positions and sizes
5. **Render**: Draw to screen buffer
6. **Diff**: Only redraw changed regions
7. **Flush**: Write to terminal

### Session Persistence

Serialize session state to JSON or TOML:

```json
{
  "name": "project-work",
  "created": "2025-10-12T14:32:00Z",
  "windows": [
    {
      "id": 0,
      "name": "editor",
      "layout": "tiled",
      "panes": [
        {
          "id": 0,
          "command": "nvim",
          "cwd": "/home/user/project",
          "active": true
        }
      ]
    }
  ]
}
```

**Note**: Cannot restore terminal state (scrollback, cursor position), only recreate panes with same commands.

---

## Integration with Nazg

### Engine Bootstrap Integration

```cpp
// In modules/engine/src/bootstrap.cpp
#include "tui/tui.hpp"

void Engine::bootstrap() {
  // ... existing setup ...

  // Register TUI command
  directive::registry::add("tui", [](auto& cctx, auto& ectx) {
    tui::TUIApp app(ectx.log, ectx.cfg, ectx.store);
    return app.run();
  });

  // Make "nazg" with no args launch TUI (optional)
  if (args_.empty()) {
    return run_tui();
  }
}
```

### Usage

```bash
# Launch TUI
nazg tui

# Or make it default
nazg
```

---

## Implementation Phases

### Phase 1: Core Infrastructure (Week 1-2)

**Goals**:
- [ ] Set up FTXUI integration in CMake
- [ ] Basic TUI app skeleton
- [ ] PTY creation and management
- [ ] Single pane rendering with shell
- [ ] Basic input handling

**Deliverables**:
- Single pane running shell
- Can send input to shell
- Can see output from shell
- Clean exit (Ctrl-D or `:quit`)

---

### Phase 2: Window Management (Week 2-3)

**Goals**:
- [ ] Pane splitting (horizontal/vertical)
- [ ] Layout engine (calculating positions)
- [ ] Pane navigation (hjkl)
- [ ] Active pane highlighting
- [ ] Pane destruction

**Deliverables**:
- Can split panes in both directions
- Can navigate between panes
- Can close panes
- Layout recalculates properly

---

### Phase 3: Modal System (Week 3-4)

**Goals**:
- [ ] Mode manager implementation
- [ ] Normal mode key handling
- [ ] Insert mode (passthrough to shell)
- [ ] Mode indicator in status bar

**Deliverables**:
- Can switch between normal and insert modes
- Normal mode: navigate with hjkl
- Insert mode: all input goes to shell
- Status bar shows current mode

---

### Phase 4: Prefix Key System (Week 4)

**Goals**:
- [ ] Prefix key detection
- [ ] Prefix timeout/indicator
- [ ] Status bar with prefix indicator
- [ ] Prefix-based command routing

**Deliverables**:
- Prefix key (C-a) triggers prefix mode
- Indicator shows in status bar
- Prefix + key executes commands
- Timeout after 1 second

---

### Phase 5: Key Bindings (Week 4-5)

**Goals**:
- [ ] KeyMap system implementation
- [ ] Default bindings (tmux-like)
- [ ] Config file loading
- [ ] Runtime binding changes via `:bind`

**Deliverables**:
- All default keybindings work
- Can load custom bindings from config
- `:bind` and `:unbind` commands work
- Help system (`:help`, `C-a ?`)

---

### Phase 6: Polish & Features (Week 5-6)

**Goals**:
- [ ] Status bar customization
- [ ] Tab bar rendering
- [ ] Scrollback buffer
- [ ] Copy/paste system (visual mode)
- [ ] Zoom pane (C-a z)
- [ ] Theme support
- [ ] Mouse support

**Deliverables**:
- Status bar fully customizable
- Can scroll through pane history
- Visual mode for text selection
- Zoom pane toggles fullscreen
- Multiple themes available

---

### Phase 7: Session Persistence (Week 6)

**Goals**:
- [ ] Session serialization (JSON/TOML)
- [ ] Auto-save on exit
- [ ] Session restore on start
- [ ] Multiple named sessions
- [ ] Session switching

**Deliverables**:
- Sessions save automatically
- Can restore previous session
- Can name and manage multiple sessions
- `:save-session` and `:load-session` work

---

## Dependencies

### CMakeLists.txt Changes

```cmake
# Find threading library
find_package(Threads REQUIRED)

# Fetch FTXUI
include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# Add TUI module
add_nazg_module(tui blackbox directive config nexus)
target_link_libraries(tui PUBLIC
  ftxui::screen
  ftxui::dom
  ftxui::component
  Threads::Threads
  util  # For forkpty() on Linux
)

# Update engine to link TUI
target_link_libraries(engine PUBLIC tui)
```

### System Dependencies

**Linux**:
```bash
# Ubuntu/Debian
sudo apt-get install libutil-dev

# Arch
sudo pacman -S util-linux
```

**macOS**:
- No additional deps (util functions in libc)

---

## Key Technical Challenges

### 1. PTY Management

**Challenge**: Creating and managing pseudo-terminals, handling child process lifecycle.

**Solution**:
- Use `posix_openpt()`, `grantpt()`, `unlockpt()`
- Proper SIGCHLD handling for dead shells
- Clean up file descriptors on pane destruction
- Handle EINTR properly in all I/O operations

### 2. Input Multiplexing

**Challenge**: Reading from multiple PTYs efficiently without blocking.

**Solution**:
- Use `select()` or `epoll()` for monitoring multiple fds
- Non-blocking reads
- Timeout-based polling for UI updates

### 3. Rendering Performance

**Challenge**: Efficient rendering without flicker.

**Solution**:
- FTXUI handles diff-based rendering
- Only update changed regions
- Double buffering for smooth updates

### 4. Terminal State

**Challenge**: Proper terminal state management across resize, suspend, etc.

**Solution**:
- Send TIOCSWINSZ on pane resize
- Handle SIGWINCH for terminal resize
- Proper terminal mode restoration on exit

### 5. Scrollback

**Challenge**: Efficiently storing and rendering scrollback history.

**Solution**:
- Ring buffer for scrollback lines
- Configurable maximum scrollback size
- Lazy rendering (only visible lines)

---

## Future Enhancements

### Post-MVP Features

1. **Detach/Reattach**: Like tmux, detach from session and reattach later
2. **Remote Sessions**: Connect to nazg TUI running on remote machines
3. **Scripting**: Lua/Python bindings for automation
4. **Pane Synchronization**: Send input to multiple panes simultaneously
5. **Search**: Search through scrollback (`C-a /`)
6. **Pane Broadcasting**: Type in one pane, broadcast to others
7. **Custom Layouts**: Save and load custom layout templates
8. **Status Line Plugins**: Extensible status bar with plugins
9. **Notification System**: Alert when pane output matches pattern
10. **Integration with Brain**: Show nazg brain suggestions in status bar

---

## Testing Strategy

### Unit Tests

- PTY creation/destruction
- Layout calculations
- Key binding lookup
- Mode transitions
- Command parsing

### Integration Tests

- Full TUI lifecycle (start, split, navigate, exit)
- Session save/restore
- Config file loading
- Multiple windows/panes

### Manual Testing

- Test with different shells (bash, zsh, fish)
- Test with different terminal emulators
- Test with different screen sizes
- Test edge cases (many panes, rapid resizing)

---

## Documentation

### User Documentation

- [ ] Getting started guide
- [ ] Keybinding reference
- [ ] Configuration options
- [ ] Command reference
- [ ] FAQ

### Developer Documentation

- [ ] Architecture overview
- [ ] Module API reference
- [ ] Adding custom commands
- [ ] Extending key bindings
- [ ] Theme development

---

## Summary

This TUI system will transform nazg into a powerful terminal multiplexer that combines:

- **tmux-like functionality**: Windows, panes, tabs, prefix keys
- **Neovim-like UX**: Modal editing, powerful keyboard navigation
- **Full customization**: Every key bindable, theme-able, configurable
- **Native integration**: Uses nazg's existing module system
- **Session persistence**: Never lose your workspace

The phased implementation approach allows for incremental delivery of value, with the core multiplexing functionality available early, and advanced features added iteratively.

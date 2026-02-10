# Nazg TUI Component Architecture

## Overview

The Nazg TUI uses a **declarative component composition system** inspired by modern UI frameworks (React, Flutter, SwiftUI). Components are Lego-like building blocks that compose into menus with parent/child hierarchies and dynamic layouts.

---

## Core Concepts

### 1. Components (Lego Blocks)

Components are reusable UI elements that can be composed together:

**Container Components** (have children):
- `Box` - Layout container (vertical/horizontal splits, borders, padding)
- `Split` - Two-pane splitter with adjustable ratio
- `Tabs` - Tabbed container

**Leaf Components** (no children):
- `MenuList` - Scrollable list with j/k navigation
- `Text` - Static or dynamic text display
- `Input` - Text input field (requires INSERT mode)
- `StatusLine` - Info/status display
- `Spinner` - Loading indicator
- `ProgressBar` - Progress visualization

### 2. Menus (Component Trees)

A **Menu** is a named collection of components arranged in a tree hierarchy:

```
MainMenu
├── Box (vertical, bordered)
│   ├── Text ("Nazg Main Menu")
│   ├── MenuList (["Git", "Tasks", "Workspace", "Terminal"])
│   └── Box (horizontal)
│       ├── Text ("Selected: ")
│       └── Text (dynamic: current_selection)
```

### 3. MenuManager

Discovers and registers menus from:
1. Built-in menus (WelcomeScreen, MainMenu)
2. Module-registered menus (GitStatus, TaskList)
3. User-defined menus (future: from config/plugins)

### 4. Navigation Stack

Menus are pushed/popped on a stack for history navigation:
```
WelcomeScreen → MainMenu → GitStatus → CommitDialog
              ←          ←           ← (:back command)
```

---

## Component API Design

### ComponentBase Interface

```cpp
namespace nazg::tui {

enum class ComponentType {
  CONTAINER,  // Has children (Box, Split, Tabs)
  LEAF        // No children (MenuList, Text, Input)
};

class ComponentBase {
public:
  virtual ~ComponentBase() = default;

  // Identity
  virtual std::string id() const = 0;
  virtual ComponentType type() const = 0;

  // Hierarchy
  virtual void add_child(std::unique_ptr<ComponentBase> child);
  virtual void remove_child(const std::string& id);
  virtual const std::vector<std::unique_ptr<ComponentBase>>& children() const;
  ComponentBase* parent() const { return parent_; }

  // Lifecycle
  virtual void on_mount() {}    // Component added to tree
  virtual void on_unmount() {}  // Component removed from tree

  // Focus (for navigation between components)
  virtual void on_focus() { focused_ = true; }
  virtual void on_blur() { focused_ = false; }
  virtual bool is_focused() const { return focused_; }
  virtual bool is_focusable() const { return false; }  // Override to true for interactive components

  // Event handling (return true if consumed)
  virtual bool handle_event(const ftxui::Event& event) { return false; }

  // Rendering
  virtual ftxui::Element render(int width, int height, const Theme& theme) = 0;

  // Layout hints (for parent containers)
  struct LayoutHints {
    // Fixed sizing (takes priority if set)
    int fixed_width = -1;   // -1 = not set
    int fixed_height = -1;  // -1 = not set

    // Constraints (honored if fixed not set)
    int min_width = 0;
    int min_height = 0;
    int max_width = -1;  // -1 = unlimited
    int max_height = -1;

    // Flex factor (used when distributing remaining space)
    float flex_grow = 1.0f;

    // Auto sizing (component calculates optimal size)
    bool auto_width = false;
    bool auto_height = false;
  };
  virtual LayoutHints get_layout_hints() const { return {}; }

  // Async loading support
  virtual bool is_loading() const { return false; }
  virtual std::string loading_message() const { return "Loading..."; }

protected:
  ComponentBase* parent_ = nullptr;
  bool focused_ = false;
  std::vector<std::unique_ptr<ComponentBase>> children_;
};

} // namespace nazg::tui
```

### Menu Base Class

```cpp
namespace nazg::tui {

class Menu {
public:
  virtual ~Menu() = default;

  // Identity
  virtual std::string id() const = 0;
  virtual std::string title() const = 0;

  // Mode support
  virtual bool supports_insert_mode() const { return false; }
  virtual bool supports_visual_mode() const { return false; }

  // State preservation (when navigating away and back)
  virtual bool preserve_state() const { return true; }  // Default: preserve

  // State management (optional, for persistence)
  struct MenuState {
    std::map<std::string, std::string> data;  // Component-specific state
  };
  virtual MenuState save_state() const { return {}; }
  virtual void restore_state(const MenuState& state) {}

  // Build the component tree (called once on load)
  virtual void build(TUIContext& ctx) = 0;

  // Get root component
  ComponentBase* root() const { return root_.get(); }

  // Lifecycle
  virtual void on_load() {}    // Menu pushed to stack
  virtual void on_unload() {}  // Menu popped from stack
  virtual void on_resume() {}  // Menu becomes active again (after :back)

protected:
  // Helper for building component trees
  void set_root(std::unique_ptr<ComponentBase> root) {
    root_ = std::move(root);
  }

private:
  std::unique_ptr<ComponentBase> root_;
};

} // namespace nazg::tui
```

---

## Declarative Menu API

### Builder Pattern Approach

```cpp
// Example: MainMenu
class MainMenu : public Menu {
public:
  std::string id() const override { return "MainMenu"; }
  std::string title() const override { return "Main Menu"; }

  void build(TUIContext& ctx) override {
    auto menu_items = std::vector<std::string>{
      "Git Status",
      "Task List",
      "Workspace",
      "Open Terminal",
      "Settings",
      "Quit"
    };

    // Create component tree using builder pattern
    set_root(
      Box::vertical()
        .border(true)
        .title("Nazg Main Menu")
        .padding(1)
        .children({

          // Header text
          Text::create("Select an option:")
            .color(Color::Cyan)
            .bold(true),

          // Menu list (main interactive component)
          MenuList::create()
            .items(menu_items)
            .on_select([this, &ctx](int index) {
              handle_selection(ctx, index);
            })
            .focusable(true),

          // Footer with keybinding hints
          Box::horizontal()
            .children({
              Text::create("j/k: Navigate"),
              Text::create(" | "),
              Text::create("Enter: Select"),
              Text::create(" | "),
              Text::create("q: Quit")
            })
            .style(theme_.status_bar)
        })
    );
  }

private:
  void handle_selection(TUIContext& ctx, int index) {
    switch (index) {
      case 0: ctx.menus().load("GitStatus"); break;
      case 1: ctx.menus().load("TaskList"); break;
      case 2: ctx.menus().load("Workspace"); break;
      case 3: ctx.commands().execute(ctx, "terminal"); break;
      case 4: ctx.menus().load("Settings"); break;
      case 5: ctx.commands().execute(ctx, "quit"); break;
    }
  }
};
```

### More Complex Example with Nesting

```cpp
// Example: GitStatusMenu with multiple panes
class GitStatusMenu : public Menu {
public:
  std::string id() const override { return "GitStatus"; }
  std::string title() const override { return "Git Status"; }

  void build(TUIContext& ctx) override {
    // Get git data
    auto status = get_git_status();
    auto branch_info = get_branch_info();

    set_root(
      Box::vertical()
        .border(true)
        .title("Git Status - " + branch_info.name)
        .children({

          // Top info bar
          Box::horizontal()
            .height(3)
            .children({
              Text::create("Branch: " + branch_info.name).color(Color::Green),
              Text::create(" | "),
              Text::create("Ahead: " + std::to_string(branch_info.ahead)),
              Text::create(" | "),
              Text::create("Behind: " + std::to_string(branch_info.behind))
            }),

          // Main split: file list | details
          Split::horizontal()
            .ratio(0.4f)  // Left takes 40%, right takes 60%
            .children({

              // Left: Changed files
              Box::vertical()
                .border(true)
                .title("Changed Files")
                .children({
                  MenuList::create()
                    .items(status.changed_files)
                    .on_select([this](int idx) { show_file_details(idx); })
                    .focusable(true)
                }),

              // Right: File diff preview
              Box::vertical()
                .border(true)
                .title("Diff Preview")
                .children({
                  Text::create(current_diff_)
                    .scrollable(true)
                })
            }),

          // Bottom: Action buttons
          Box::horizontal()
            .height(3)
            .children({
              Text::create("[c] Commit"),
              Text::create(" [p] Push"),
              Text::create(" [f] Fetch"),
              Text::create(" [b] Back")
            })
        })
    );
  }

private:
  std::string current_diff_;
  void show_file_details(int idx) { /* ... */ }
};
```

---

## MenuManager API

```cpp
namespace nazg::tui {

class MenuManager {
public:
  // Registration
  using MenuFactory = std::function<std::unique_ptr<Menu>()>;
  void register_menu(const std::string& id, MenuFactory factory);

  // Navigation
  bool load(const std::string& menu_id);  // Push menu onto stack
  bool back();   // Pop current menu, show previous
  bool forward(); // Re-push menu (if available)

  // Query
  Menu* current() const;  // Top of stack
  std::vector<std::string> list_registered() const;
  bool is_registered(const std::string& id) const;
  size_t stack_depth() const;

  // Lifecycle
  void clear_stack();  // Pop all menus

private:
  struct MenuStackEntry {
    std::unique_ptr<Menu> menu;
    Menu::MenuState saved_state;  // Preserved when navigating away
  };

  std::map<std::string, MenuFactory> factories_;
  std::vector<MenuStackEntry> stack_;        // Menu navigation stack
  std::vector<std::string> forward_stack_;   // For forward navigation
};

// Macro for easier registration
#define REGISTER_MENU(ctx, MenuClass) \
  (ctx).menus().register_menu(#MenuClass, []() { return std::make_unique<MenuClass>(); })

} // namespace nazg::tui
```

---

## Component Implementation Examples

### Box (Container)

```cpp
class Box : public ComponentBase {
public:
  enum class Direction { HORIZONTAL, VERTICAL };

  static std::unique_ptr<Box> vertical() {
    auto box = std::make_unique<Box>();
    box->direction_ = Direction::VERTICAL;
    return box;
  }

  static std::unique_ptr<Box> horizontal() {
    auto box = std::make_unique<Box>();
    box->direction_ = Direction::HORIZONTAL;
    return box;
  }

  // Builder methods - Layout
  Box& border(bool enabled) { border_ = enabled; return *this; }
  Box& title(const std::string& t) { title_ = t; return *this; }
  Box& padding(int p) { padding_ = p; return *this; }
  Box& children(std::vector<std::unique_ptr<ComponentBase>> c) {
    children_ = std::move(c);
    return *this;
  }

  // Builder methods - Sizing
  Box& width(int w) { layout_hints_.fixed_width = w; return *this; }
  Box& height(int h) { layout_hints_.fixed_height = h; return *this; }
  Box& min_width(int w) { layout_hints_.min_width = w; return *this; }
  Box& max_width(int w) { layout_hints_.max_width = w; return *this; }
  Box& min_height(int h) { layout_hints_.min_height = h; return *this; }
  Box& max_height(int h) { layout_hints_.max_height = h; return *this; }
  Box& flex(float f) { layout_hints_.flex_grow = f; return *this; }

  // ComponentBase implementation
  ComponentType type() const override { return ComponentType::CONTAINER; }

  ftxui::Element render(int width, int height, const Theme& theme) override {
    std::vector<ftxui::Element> rendered_children;

    // Render all children with dynamic sizing
    for (auto& child : children_) {
      // Calculate child size based on flex factors
      int child_width = calculate_child_width(child.get(), width);
      int child_height = calculate_child_height(child.get(), height);
      rendered_children.push_back(child->render(child_width, child_height, theme));
    }

    // Combine children based on direction
    ftxui::Element content;
    if (direction_ == Direction::VERTICAL) {
      content = ftxui::vbox(std::move(rendered_children));
    } else {
      content = ftxui::hbox(std::move(rendered_children));
    }

    // Apply padding
    if (padding_ > 0) {
      content = content | ftxui::padding(padding_);
    }

    // Apply border
    if (border_) {
      content = content | ftxui::border;
      if (!title_.empty()) {
        content = content | ftxui::title(title_);
      }
    }

    return content;
  }

private:
  Direction direction_ = Direction::VERTICAL;
  bool border_ = false;
  std::string title_;
  int padding_ = 0;

  int calculate_child_width(ComponentBase* child, int available_width);
  int calculate_child_height(ComponentBase* child, int available_height);
};
```

### MenuList (Leaf)

```cpp
class MenuList : public ComponentBase {
public:
  static std::unique_ptr<MenuList> create() {
    return std::make_unique<MenuList>();
  }

  // Builder methods - Content
  MenuList& items(const std::vector<std::string>& i) {
    items_ = i;
    return *this;
  }

  MenuList& on_select(std::function<void(int)> callback) {
    on_select_ = callback;
    return *this;
  }

  MenuList& focusable(bool f) {
    focusable_ = f;
    return *this;
  }

  // Builder methods - Sizing
  MenuList& width(int w) { layout_hints_.fixed_width = w; return *this; }
  MenuList& height(int h) { layout_hints_.fixed_height = h; return *this; }
  MenuList& flex(float f) { layout_hints_.flex_grow = f; return *this; }

  // ComponentBase implementation
  ComponentType type() const override { return ComponentType::LEAF; }
  bool is_focusable() const override { return focusable_; }

  bool handle_event(const ftxui::Event& event) override {
    if (!focused_) return false;

    if (event.character() == "j" || event == ftxui::Event::ArrowDown) {
      selected_ = std::min(selected_ + 1, static_cast<int>(items_.size()) - 1);
      return true;
    }
    if (event.character() == "k" || event == ftxui::Event::ArrowUp) {
      selected_ = std::max(selected_ - 1, 0);
      return true;
    }
    if (event == ftxui::Event::Return) {
      if (on_select_) on_select_(selected_);
      return true;
    }
    return false;
  }

  ftxui::Element render(int width, int height, const Theme& theme) override {
    std::vector<ftxui::Element> rendered_items;

    for (size_t i = 0; i < items_.size(); ++i) {
      auto item_text = items_[i];
      auto element = ftxui::text(item_text);

      if (i == selected_) {
        element = element | ftxui::bgcolor(theme.selection_bg) | ftxui::color(theme.selection_fg);
        if (focused_) {
          element = element | ftxui::bold;
        }
      }

      rendered_items.push_back(element);
    }

    return ftxui::vbox(std::move(rendered_items)) | ftxui::vscroll_indicator | ftxui::frame;
  }

private:
  std::vector<std::string> items_;
  int selected_ = 0;
  bool focusable_ = false;
  std::function<void(int)> on_select_;
};
```

---

## Integration with TUIContext

```cpp
class TUIContext {
public:
  // Existing managers
  KeyManager& keys();
  ModeManager& modes();
  CommandManager& commands();

  // NEW: Menu management
  MenuManager& menus() { return menu_manager_; }
  const MenuManager& menus() const { return menu_manager_; }

  // NEW: Active menu
  Menu* active_menu();
  const Menu* active_menu() const;

  // Focus management (for component navigation)
  ComponentBase* focused_component();
  bool focus_next_component();      // Tab key (sequential)
  bool focus_previous_component();  // Shift+Tab (sequential)
  bool focus_component_left();      // h key (spatial)
  bool focus_component_right();     // l key (spatial)
  bool focus_component_up();        // k key (spatial)
  bool focus_component_down();      // j key (spatial)

  // Mode context (which mode keys are active)
  bool insert_mode_enabled() const {
    auto* menu = active_menu();
    return menu && menu->supports_insert_mode() && modes().current() == Mode::INSERT;
  }

private:
  MenuManager menu_manager_;
  ComponentBase* focused_component_ = nullptr;
};
```

---

## Updated Event Routing

```cpp
bool TUIApp::handle_input(Event event) {
  Mode current_mode = ctx_->modes().current();

  // 1. CommandBar (highest priority - always accessible)
  if (ctx_->command_bar().active()) {
    return ctx_->command_bar().handle_event(event, *ctx_);
  }

  // 2. Check for global commands (quit, help, etc. - work in any mode)
  if (auto cmd = ctx_->keys().lookup(event, Mode::GLOBAL, false)) {
    return ctx_->commands().execute(*ctx_, *cmd);
  }

  // 3. INSERT mode - component has exclusive event handling
  if (current_mode == Mode::INSERT) {
    // Only ESC exits INSERT mode
    if (event == Event::Escape) {
      ctx_->modes().enter(Mode::NORMAL);
      ctx_->set_status_message("NORMAL mode");
      return true;
    }

    // All other keys go to focused component
    if (auto* comp = ctx_->focused_component()) {
      return comp->handle_event(event);
    }
    return false;
  }

  // 4. Prefix mode (Ctrl-B commands)
  if (ctx_->modes().is_prefix_active()) {
    return handle_prefix_input(event);
  }

  // 5. Active menu's focused component (with bubbling)
  if (auto* menu = ctx_->active_menu()) {
    if (auto* comp = ctx_->focused_component()) {
      // Try to handle in component (bubbles up if not handled)
      if (handle_component_event_with_bubbling(comp, event)) {
        return true;
      }
    }
  }

  // 6. Focus navigation (Tab, Shift+Tab, hjkl)
  if (event == Event::Tab) {
    ctx_->focus_next_component();
    return true;
  }
  if (event == Event::TabReverse) {
    ctx_->focus_previous_component();
    return true;
  }

  // hjkl spatial navigation (only in NORMAL mode)
  if (current_mode == Mode::NORMAL) {
    if (event.character() == "h") return ctx_->focus_component_left();
    if (event.character() == "l") return ctx_->focus_component_right();
    if (event.character() == "k") return ctx_->focus_component_up();
    if (event.character() == "j") return ctx_->focus_component_down();
  }

  // 7. Mode-specific bindings
  if (auto cmd = ctx_->keys().lookup(event, current_mode, false)) {
    return ctx_->commands().execute(*ctx_, *cmd);
  }

  return false;
}

// Helper: Event bubbling up component tree
bool TUIApp::handle_component_event_with_bubbling(ComponentBase* comp, Event event) {
  if (!comp) return false;

  // Try current component
  if (comp->handle_event(event)) {
    return true;
  }

  // Bubble to parent
  if (comp->parent()) {
    return handle_component_event_with_bubbling(comp->parent(), event);
  }

  return false;
}
```

---

## Menu Discovery

Built-in menus are registered in `TUIContext::initialize()`:

```cpp
void TUIContext::initialize() {
  // Register built-in menus
  menu_manager_.register_menu("WelcomeScreen",
    []() { return std::make_unique<WelcomeScreen>(); });
  menu_manager_.register_menu("MainMenu",
    []() { return std::make_unique<MainMenu>(); });

  // External modules register their menus via callback
  // (Called from engine after TUI initialization)
  for (auto& callback : module_init_callbacks_) {
    callback(*this);
  }

  // Load default menu
  std::string startup = cfg_->get_string("tui", "startup_menu", "WelcomeScreen");
  menu_manager_.load(startup);
}
```

External modules register during initialization:

```cpp
// In modules/git/src/git.cpp
void register_git_tui(tui::TUIContext& ctx) {
  ctx.menus().register_menu("GitStatus",
    []() { return std::make_unique<GitStatusMenu>(); });

  ctx.commands().register_command("git-status", "Show git status",
    [](auto& ctx, auto& args) {
      return ctx.menus().load("GitStatus");
    });
}
```

---

## Commands

New commands for menu navigation:

```
:load <menu>     - Load a menu (push onto stack)
:back            - Go back to previous menu
:forward         - Go forward (if available)
:menus           - List all registered menus
:menu-info <id>  - Show info about a menu
:terminal        - Switch to terminal mode
```

---

## Implementation Checklist

### Phase 1: Foundation
- [ ] ComponentBase interface with hierarchy support
- [ ] Menu base class
- [ ] MenuManager with stack navigation

### Phase 2: Core Components
- [ ] Box container (vertical/horizontal)
- [ ] MenuList leaf component
- [ ] Text leaf component
- [ ] Split container

### Phase 3: Built-in Menus
- [ ] WelcomeScreen menu
- [ ] MainMenu menu

### Phase 4: Integration
- [ ] Update TUIApp render pipeline
- [ ] Event routing with component focus
- [ ] `:load`, `:back`, `:forward` commands
- [ ] Mode integration (INSERT mode per-menu)

### Phase 5: Developer Experience
- [ ] Builder pattern API refinement
- [ ] Example module (git or task) with menu
- [ ] Documentation and examples

---

## Design Decisions (Confirmed)

### 1. Builder API Style
**Decision**: Fluent builder pattern with method chaining
```cpp
Box::vertical().border(true).title("Menu").children({...})
```

### 2. Component State Persistence
**Decision**: Preserve state by default, configurable per-menu
- MenuList remembers selection and scroll position
- Text inputs remember content
- Menus can override via `virtual bool preserve_state() const { return false; }`
- State only preserved during session (not persisted to disk)

### 3. Layout Sizing Options
**Decision**: Support all sizing strategies
- **Fixed sizes**: `.width(20)`, `.height(10)`
- **Min/max constraints**: `.min_width(10).max_width(50)`
- **Flex factors**: `.flex(1.5)` for proportional sizing
- **Auto sizing**: Component calculates optimal size based on content

### 4. Focus Navigation
**Decision**: Support both Tab and hjkl navigation
- **Tab/Shift+Tab**: Sequential focus cycling through focusable components
- **hjkl**: Spatial navigation (move focus in direction)
- Both work simultaneously, developer chooses based on layout

### 5. INSERT Mode Scoping
**Decision**: INSERT mode is component-scoped
- Text input component active + user presses 'i' → enter INSERT mode for that component
- While in INSERT mode: only that component receives key events
- Escape exits INSERT mode, returns to NORMAL
- Global keys (quit, help) and command bar remain accessible
- Menu must declare `supports_insert_mode() = true`

### 6. Event Bubbling
**Decision**: Events bubble up parent chain if not handled
- Child component tries to handle event first
- If `handle_event()` returns false, event bubbles to parent
- Continues until handled or reaches menu root
- Menu can implement fallback handlers

### 7. Async Data Loading
**Decision**: Support both sync and async, prefer async
- **Async pattern** (preferred): Show loading indicator, update when data ready
- **Sync pattern** (fallback): Block during load for simple cases
- Components implement `virtual bool is_loading() const { return false; }`
- Loading state shows progress bar or spinner

### 8. Menu Registration
**Decision**: Provide registration macro for convenience
```cpp
REGISTER_MENU(GitStatusMenu);
```

### 9. Component Communication
Sibling components communicate via:
- Shared state in parent menu
- Callbacks passed during construction
- Event system (future enhancement)

# Nazg TUI Improvement Plan

This document captures the next wave of upgrades for the nazg TUI. It distills
lessons from the `ctrlCore` engine and the `controlCenter` pilot and maps them
onto the current nazg architecture (`TUIApp → TUIContext → Window/Layout/Pane`).
Each section below is meant to become a short, trackable doc note so progress
stays visible.

---

## 1. Architecture Narrative

- Document the present pipeline (`TUIApp` loop, `TUIContext`, `Window/Layout` tree).
- Add a "current vs. target" diagram that contrasts nazg's window/pane stack
  with ctrlCore's `Engine → LayoutManager → ComponentBase` flow.
- Highlight gaps to close: reusable component base, command bar layer, richer
  layout hints, predictable key dispatch.

**PRIORITY UPDATE:** Implement declarative component composition system:
- **Component-based Lego architecture**: Reusable components compose into menus with parent/child hierarchies
- **MenuManager**: Discovers and registers menus from built-in and module sources
- **Navigation stack**: Menu history for :back/:forward navigation
- **Declarative API**: Intuitive builder pattern for composing menus (e.g., `Box::vertical().children({...})`)
- **Mode integration**: Menus declare if they need INSERT mode, keyboard input only works if supported
- **See detailed design**: `docs/tui_component_architecture.md`

## 2. Input Flow & Keybinding Strategy

- Explain how modes, prefix handling, and `KeyManager::lookup` work today.
- ~~Record the `Ctrl-b x` miss as a regression~~ **VERIFIED WORKING**
- Adopt ctrlCore's deterministic key registry idea:
  - **Per-component bindings with explicit registration** (PRIMARY GOAL)
  - Automatic log entries for every key → command resolution.
  - JSON/YAML manifest describing default bindings.
- Action items:
  1. **Implement `KeyManager::register_component_keys(component_id, bindings)`**
  2. Add a logging checklist for key dispatch: key string, mode, command name.
  3. Write unit tests for prefix bindings and component-specific keys.
  4. Define a binding-override file format.

**Implementation Notes:**
- Components should be able to register/unregister their own keybindings
- When a component is active (focused), its bindings take priority
- Global bindings remain accessible unless explicitly overridden
- Example: MenuList component registers j/k/enter when focused

## 3. Command Surface & Command Bar

- List all prefix commands (split, kill, navigate, zoom) in the docs.
- Describe the desired command bar (like ctrlCore’s `CommandBar`):
  - `:` to enter, command registry with help strings, argument-aware callbacks.
  - Blackbox logging for executed commands and failures.
- Planned work:
  - Introduce a `CommandBar` component integrated with `TUIContext`.
  - Route prefix commands through the same registry (so both `Ctrl-b x` and
    `:kill-pane` hit identical code paths).

## 4. Layout & Component Model

- Document the existing binary `Layout::Node` split tree and rendering fallback.
- **ACTIVE WORK:** Implement "component" interface inspired by ctrlCore's `ComponentBase`:
  - Base interface: `render(width, height, theme) → Element`
  - Focus management: `on_focus()`, `on_blur()`, `is_focused()`
  - Event handling: `handle_event(event) → bool`
  - Lifecycle: `on_activate()`, `on_deactivate()`
  - Panes, menus, popups, status bar, command bar all implement this interface
  - `ComponentRegistry` in `TUIContext` holds registered components
  - Persistent split ratios and saved layouts.
- Update docs with explicit goals: pane focus rules, cross-window copying,
  layout serialization.

**Component Types to Implement:**

**Container Components** (have children, handle layout):
1. **Box** - Vertical/horizontal container with borders, padding, titles
2. **Split** - Two-pane splitter with adjustable ratio
3. **Tabs** - Tabbed container for multiple views

**Leaf Components** (interactive, no children):
1. **MenuList** - Scrollable list with j/k navigation, enter to select
2. **Text** - Static or dynamic text display (can be scrollable)
3. **Input** - Text input field (requires menu to support INSERT mode)
4. **StatusLine** - Info/status display

**Menus** (top-level component trees):
1. **WelcomeScreen** - Default startup (like neovim's start screen)
2. **MainMenu** - Uses Box + MenuList to show module options
3. **Example module menus** - Git status, task list, etc.

**See**: `docs/tui_component_architecture.md` for full API design

## 5. Window/Pane Lifecycle & Logging

- Record the desired lifecycle: create → render → split → close → recycle.
- Note current logging improvements (`Window::split_active`, `close_active`)
  and keep the sequencing visible in the docs.
- Keep an open section for known bugs (prefix binding) until fully fixed.
- Add a testing checklist: simulated split/close sequences, zoom toggle,
  multi-window interplay.

## 6. Status Bar & Diagnostics

- Describe data shown today (prefix indicator, PID alive flag, zoom status,
  window summary).
- Plan enhancements:
  - Active command display, last input, layout mode indicator.
  - Toggleable debug pane with real-time logs.
  - Quick help overlay for available commands (`Ctrl-b ?`, `:help`).

## 7. Terminal & Event Handling

- Explain the current threading model (UI loop + 60 fps update thread).
- Document ctrlCore’s alternative (single-threaded raw `read` loop) and why
  we might adopt a coroutine/event-pump approach.
- Roadmap items:
  - Reduce `Event::Custom` spam.
  - Ensure prefix timeout is accurate without sleeping.
  - Explore FTXUI’s async APIs or custom input pump.

## 8. Extensibility & Plugins

- **PRIMARY GOAL:** Allow other nazg modules to inject menus/components into TUI.
- Reference ctrlCore's `REGISTER_MENU` macro as an example.
- **Planned steps:**
  1. Create `ComponentBase` interface all components must implement
  2. Add `ComponentRegistry` to `TUIContext` for dynamic registration
  3. Provide `REGISTER_COMPONENT("id", ComponentType)` API
  4. Document component lifecycle callbacks
  5. Allow modules to register keybindings via `TUIContext`
  6. Implement `:load <component>` command for dynamic loading

**Example Module Integration:**
```cpp
// In modules/git/src/tui_integration.cpp
#include "tui/tui_api.hpp"

class GitStatusMenu : public tui::ComponentBase {
  // Implementation...
};

void register_git_tui(tui::TUIContext& ctx) {
  ctx.components().register_component("GitStatus",
    std::make_unique<GitStatusMenu>());
}
```

**Workflow:**
1. User runs `nazg` → WelcomeScreen shown
2. User types `:load MainMenu` → MainMenu component activated
3. User navigates menu with j/k, presses enter
4. Selected module's component loads (e.g., GitStatus)

## 9. Debugging & Testing Playbook

- Create a quick reference for enabling verbose logs
  (`NAZG_LOG_CONSOLE=1` and `blackbox.min_level=DEBUG`).
- Document how to tail TUI logs, interpret prefix messages, and reproduce ctrlCore’s SIGINT handling.
- Add automated tests to the roadmap:
  - Binding regression tests (`Ctrl-b x`, arrow navigation).
  - Split/close/zoom combos.
  - Command execution via prefix and command bar.

## 10. Roadmap Snapshot

- **Short term (CURRENT SPRINT)**
  - ✅ ~~Fix prefix lookup regression~~ (verified working)
  - ✅ Document component architecture (see `tui_component_architecture.md`)
  - 🔄 Implement `ComponentBase` interface with parent/child hierarchy
  - 🔄 Implement `Menu` base class
  - 🔄 Create `MenuManager` with navigation stack
  - 🔄 Implement `Box` container component (vertical/horizontal layouts)
  - 🔄 Implement `MenuList` leaf component with j/k/enter navigation
  - 🔄 Implement `Text` leaf component
  - 🔄 Create `WelcomeScreen` menu
  - 🔄 Create `MainMenu` using declarative API
  - 🔄 Add `:load`, `:back`, `:forward` commands
  - 🔄 Update TUIApp render pipeline for menu rendering
  - 🔄 Integrate mode system (per-menu INSERT mode support)

- **Medium term**
  - Add automated tests for keybindings and components
  - Wire command bar with enhanced logging
  - Introduce persistent split ratios
  - Implement layout serialization (save/restore)
  - Expose keybinding overrides and user config merging
  - Create example modules (git, task) with TUI integration

- **Long term**
  - Full plugin system for components/menus
  - Session persistence (windows, panes, commands, loaded components)
  - Advanced visual components (graphs, metrics, log viewers)
  - Multi-window component support

---

**Status:** Architecture design complete with all key decisions documented:
- ✅ Component hierarchy with parent/child relationships
- ✅ Declarative builder API (.border().title().children())
- ✅ Menu navigation stack with state preservation
- ✅ INSERT mode scoping (per-component, ESC to exit)
- ✅ Event bubbling up component tree
- ✅ Both Tab and hjkl focus navigation
- ✅ All layout sizing options (fixed, min/max, flex, auto)
- ✅ Async loading with progress indicators
- ✅ Registration macro for menus

**See**: `docs/tui_component_architecture.md` for complete API specifications and examples.

**Next Step:** Begin implementation starting with core interfaces (ComponentBase, Menu, MenuManager).


  1. Adjustable splits (like tmux/neovim)
  2. Multi-select menu loading (open multiple menus in split view)
  3. Dynamic text updates (reactive content)

  Updated Architecture

  Core Changes Needed

  1. Menu Container System

  The current single-menu stack isn't sufficient. We need:

  MenuWorkspace
  ├── Split (horizontal 50/50)
  │   ├── Menu 1 (GitStatus)
  │   └── Menu 2 (TaskList)

  New concept: MenuWorkspace - manages multiple active menus in a split layout
  - Replaces simple menu stack
  - Each workspace node can be: Menu, HSplit, VSplit
  - Focus tracking (which menu is active)
  - Resize handles between splits

  2. Split Component (in addition to Box)

  class Split : public ComponentBase {
    // Two children with adjustable ratio
    float ratio_;  // 0.0 - 1.0
    bool horizontal_;
    bool resizing_ = false;

    // Resize via mouse or keys (Ctrl+W, <, >)
  };

  3. MenuList Multi-Select

  class MenuList : public ComponentBase {
    std::set<int> selected_indices_;  // Multi-select
    bool multi_select_enabled_ = false;

    // Space to toggle, Enter to confirm selection
    std::function<void(std::vector<int>)> on_multi_select_;
  };

  4. Dynamic Text

  class Text : public ComponentBase {
    std::function<std::string()> content_fn_;  // Dynamic
    std::chrono::milliseconds update_interval_{100};

    // Polls content_fn_ on each render
  };

  Revised File Structure

  modules/tui/include/tui/
  ├── components/
  │   ├── box.hpp                  # Static layout container
  │   ├── split.hpp                # NEW: Adjustable split (2 children)
  │   ├── menu_list.hpp            # Multi-select support
  │   └── text.hpp                 # Dynamic content support
  ├── managers/
  │   └── menu_workspace.hpp       # NEW: Multi-menu layout manager
  └── menus/
      ├── welcome_screen.hpp
      └── main_menu.hpp            # NEW: Directory-style menu

  Example: Main Menu with Multi-Select

  class MainMenu : public Menu {
    void build(TUIContext& ctx) override {
      auto menu_items = std::vector<std::string>{
        "Git Status",
        "Task List",
        "Workspace",
        "Terminal"
      };

      set_root(
        Box::vertical()
          .border(true)
          .title("Main Menu - Select one or more")
          .children({
            Text::create("Use Space to select, Enter to open"),

            MenuList::create()
              .items(menu_items)
              .multi_select(true)  // NEW
              .on_multi_select([&](std::vector<int> indices) {
                handle_multi_selection(ctx, indices);
              })
              .focusable(true)
          })
      );
    }

    void handle_multi_selection(TUIContext& ctx, std::vector<int> indices) {
      if (indices.empty()) return;

      // Map indices to menu IDs
      std::vector<std::string> menu_ids;
      for (int idx : indices) {
        switch(idx) {
          case 0: menu_ids.push_back("GitStatus"); break;
          case 1: menu_ids.push_back("TaskList"); break;
          case 2: menu_ids.push_back("Workspace"); break;
          case 3: menu_ids.push_back("Terminal"); break;
        }
      }

      // Open menus in split view
      ctx.workspace().open_split(menu_ids);
    }
  };

  MenuWorkspace Manager

  class MenuWorkspace {
  public:
    // Split layout tree
    struct Node {
      enum Type { MENU, HSPLIT, VSPLIT };
      Type type;

      std::unique_ptr<Menu> menu;        // If type == MENU
      std::unique_ptr<Node> left, right; // If type == SPLIT
      float ratio = 0.5f;                // Split ratio
    };

    // Open single menu (replaces current)
    void open(const std::string& menu_id);

    // Open multiple menus in split view
    void open_split(const std::vector<std::string>& menu_ids);

    // Split current focused menu
    void split_horizontal();
    void split_vertical();

    // Resize splits
    void resize_active_split(int delta);  // Adjust ratio

    // Focus management
    Menu* focused_menu();
    void focus_next_split();
    void focus_previous_split();

    // Render the tree
    ComponentBase* root_component();

  private:
    std::unique_ptr<Node> root_;
    Node* focused_node_ = nullptr;
  };

  Split Component Implementation

  class Split : public ComponentBase {
  public:
    enum Direction { HORIZONTAL, VERTICAL };

    static std::unique_ptr<Split> horizontal(
      std::unique_ptr<ComponentBase> left,
      std::unique_ptr<ComponentBase> right,
      float ratio = 0.5f
    );

    // Adjust ratio (0.0 - 1.0)
    void set_ratio(float r) { ratio_ = std::clamp(r, 0.1f, 0.9f); }
    float ratio() const { return ratio_; }

    // Handle resize events
    bool handle_event(const ftxui::Event& event) override;

    // Render with separator
    ftxui::Element render(int width, int height, const Theme& theme) override;

  private:
    Direction direction_;
    float ratio_;  // 0.0 = all left, 1.0 = all right
    bool resize_mode_ = false;
  };

  Dynamic Text Example

  // In GitStatusMenu
  auto status_text = Text::create()
    .dynamic([this]() {
      return get_current_branch() + " | " +
             std::to_string(get_uncommitted_changes()) + " changes";
    })
    .update_interval(std::chrono::seconds(1));

  ---
  Updated Implementation Plan

  Phase 1: Core Components (Simple)

  1. Text (static + dynamic) - ~150 LOC
  2. Box (static layout) - ~300 LOC
  3. MenuList (single-select) - ~150 LOC

  Phase 2: Split Support

  4. Split component (adjustable) - ~400 LOC
  5. MenuWorkspace manager - ~500 LOC
  6. Resize handlers (keyboard + mouse) - ~200 LOC

  Phase 3: Multi-Select & Integration

  7. MenuList multi-select mode - ~100 LOC
  8. MainMenu with directory pattern - ~100 LOC
  9. TUIApp integration with workspace - ~150 LOC

  Phase 4: Polish

  10. WelcomeScreen - ~50 LOC
  11. Focus indicators in splits - ~100 LOC
  12. Keybindings (Ctrl-W for split nav, etc.) - ~100 LOC

  Total: ~2,200 LOC (significantly more complex)

  ---
  New Commands Needed

  :split-horizontal   - Split current menu horizontally
  :split-vertical     - Split current menu vertically
  :close-split        - Close current split pane
  :resize +10         - Increase split ratio
  :resize -10         - Decrease split ratio
  :focus-next         - Focus next split
  :focus-prev         - Focus previous split

  Keybindings (tmux-like)

  Ctrl-W + h/j/k/l    - Focus split in direction
  Ctrl-W + H/J/K/L    - Resize split
  Ctrl-W + s          - Split horizontal
  Ctrl-W + v          - Split vertical
  Ctrl-W + c          - Close split
  Ctrl-W + =          - Equalize splits

  ---
  Critical Design Decision

  This multi-menu split architecture is substantially more complex than the original single-menu design. It
  essentially combines:
  - Menu system (your requested feature)
  - Window/pane multiplexer (like tmux)

  Two approaches:

  Approach A: Full Multi-Menu Splits (as described above)

  Pros:
  - Extremely powerful (multiple menus visible)
  - Matches your vision
  - Enables complex workflows

  Cons:
  - ~2,200 LOC to implement
  - Complex state management
  - 2-3 weeks of work
  - May delay other features

  Approach B: Phased Rollout

  Phase 1: Single menu at a time (original plan)
  - Simple, works quickly (~720 LOC)
  - Get menus working end-to-end
  - 3-5 days

  Phase 2: Add multi-menu splits later
  - Build on proven foundation
  - Add Split/MenuWorkspace
  - 1-2 weeks additional

  ---


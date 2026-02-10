#include "tui/tui_context.hpp"
#include "blackbox/logger.hpp"
#include "config/config.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>

namespace nazg::tui {

TUIContext::TUIContext(nazg::blackbox::logger* log, nazg::config::store* cfg)
    : log_(log), cfg_(cfg), menu_manager_(log) {

  // Get default shell
  const char* shell_env = std::getenv("SHELL");
  if (shell_env && shell_env[0] != '\0') {
    default_shell_ = shell_env;
  } else {
    default_shell_ = "/bin/sh";
  }

  if (cfg_) {
    default_shell_ = cfg_->get_string("tui", "default_shell", default_shell_);
  }

  log_info("TUIContext created with shell: " + default_shell_);
  log_info("Loaded " +
           std::to_string(key_manager_.get_bindings(Mode::PREFIX, true).size()) +
           " prefix key bindings");
}

void TUIContext::initialize() {
  // Set context in menu manager so it can build menus
  menu_manager_.set_context(this);

  // Register built-in commands directly here where TUIContext is fully defined
  // This avoids circular dependency issues with command_manager.cpp

  // Window management commands
  command_manager_.register_command("new-window", "Create a new window", [](auto& ctx, auto& args) {
    std::string shell = args.empty() ? "" : args[0];
    auto* win = ctx.create_window(shell);
    if (win) {
      ctx.set_status_message("Created window: " + win->name());
      return true;
    }
    ctx.set_status_message("Failed to create window");
    return false;
  });

  command_manager_.register_command("kill-window", "Close current window", [](auto& ctx, auto& args) {
    if (ctx.close_active_window()) {
      ctx.set_status_message("Closed window");
      return true;
    }
    ctx.set_status_message("Cannot close last window");
    return false;
  });

  command_manager_.register_command("next-window", "Switch to next window", [](auto& ctx, auto& args) {
    if (ctx.focus_next_window()) {
      ctx.set_status_message("Next window");
      return true;
    }
    ctx.set_status_message("Only one window");
    return false;
  });

  command_manager_.register_command("previous-window", "Switch to previous window", [](auto& ctx, auto& args) {
    if (ctx.focus_previous_window()) {
      ctx.set_status_message("Previous window");
      return true;
    }
    ctx.set_status_message("Only one window");
    return false;
  });

  command_manager_.register_command("last-window", "Switch to last window", [](auto& ctx, auto& args) {
    if (ctx.focus_last_window()) {
      ctx.set_status_message("Last window");
      return true;
    }
    ctx.set_status_message("No previous window");
    return false;
  });

  command_manager_.register_command("rename-window", "Rename current window", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) {
      ctx.set_status_message("No active window");
      return false;
    }
    if (args.empty()) {
      ctx.set_status_message("Usage: rename-window <name>");
      return false;
    }
    win->set_name(args[0]);
    ctx.set_status_message("Renamed window to: " + args[0]);
    return true;
  });

  // Pane management commands
  command_manager_.register_command("split-vertical", "Split pane vertically", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) {
      ctx.set_status_message("No active window");
      return false;
    }
    std::string shell = args.empty() ? "" : args[0];
    Pane* pane = win->split_active(SplitDirection::HORIZONTAL, shell);
    if (pane) {
      ctx.set_status_message("Split vertical");
      return true;
    }
    ctx.set_status_message("Split failed");
    return false;
  });

  command_manager_.register_command("split-horizontal", "Split pane horizontally", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) {
      ctx.set_status_message("No active window");
      return false;
    }
    std::string shell = args.empty() ? "" : args[0];
    Pane* pane = win->split_active(SplitDirection::VERTICAL, shell);
    if (pane) {
      ctx.set_status_message("Split horizontal");
      return true;
    }
    ctx.set_status_message("Split failed");
    return false;
  });

  command_manager_.register_command("kill-pane", "Close current pane", [](auto& ctx, auto& args) {
    ctx.log_info("[kill-pane] Command invoked");
    Window* win = ctx.active_window();
    if (!win) {
      ctx.log_warn("[kill-pane] No active window");
      ctx.set_status_message("No active window");
      return false;
    }
    ctx.log_info("[kill-pane] Calling win->close_active()");
    bool result = win->close_active();
    if (result) {
      ctx.log_info("[kill-pane] Successfully closed pane");
      ctx.set_status_message("Closed pane");
      return true;
    }
    ctx.log_warn("[kill-pane] Could not close pane (last one?)");
    ctx.set_status_message("Cannot close last pane");
    return false;
  });

  command_manager_.register_command("save-layout", "Save window layout", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) {
      ctx.set_status_message("No active window");
      return false;
    }
    if (win->save_layout()) {
      ctx.set_status_message("Layout saved");
      ctx.log_info("[save-layout] Layout saved");
      return true;
    }
    ctx.set_status_message("Could not save layout");
    ctx.log_warn("[save-layout] Failed to save layout");
    return false;
  });

  command_manager_.register_command("restore-layout", "Restore saved window layout", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) {
      ctx.set_status_message("No active window");
      return false;
    }
    if (win->restore_layout()) {
      ctx.set_status_message("Layout restored");
      ctx.log_info("[restore-layout] Layout restored");
      return true;
    }
    ctx.set_status_message("No saved layout");
    ctx.log_warn("[restore-layout] Failed to restore layout");
    return false;
  });

  command_manager_.register_command("toggle-zoom", "Toggle pane zoom", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) {
      ctx.set_status_message("No active window");
      return false;
    }
    if (win->toggle_zoom()) {
      ctx.set_status_message(win->is_zoomed() ? "Zoom on" : "Zoom off");
      return true;
    }
    ctx.set_status_message("Zoom not available");
    return false;
  });

  // Navigation commands
  command_manager_.register_command("navigate-left", "Navigate to pane on left", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) return false;
    if (win->navigate('h')) {
      ctx.set_status_message("Navigate: left");
      return true;
    }
    return false;
  });

  command_manager_.register_command("navigate-right", "Navigate to pane on right", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) return false;
    if (win->navigate('l')) {
      ctx.set_status_message("Navigate: right");
      return true;
    }
    return false;
  });

  command_manager_.register_command("navigate-up", "Navigate to pane above", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) return false;
    if (win->navigate('k')) {
      ctx.set_status_message("Navigate: up");
      return true;
    }
    return false;
  });

  command_manager_.register_command("navigate-down", "Navigate to pane below", [](auto& ctx, auto& args) {
    Window* win = ctx.active_window();
    if (!win) return false;
    if (win->navigate('j')) {
      ctx.set_status_message("Navigate: down");
      return true;
    }
    return false;
  });

  // Mode commands
  command_manager_.register_command("enter-insert-mode", "Enter insert mode", [](auto& ctx, auto& args) {
    ctx.modes().enter(Mode::INSERT);
    ctx.set_status_message("INSERT mode");
    return true;
  });

  command_manager_.register_command("enter-normal-mode", "Enter normal mode", [](auto& ctx, auto& args) {
    ctx.modes().enter(Mode::NORMAL);
    ctx.set_status_message("NORMAL mode");
    return true;
  });

  command_manager_.register_command("enter-visual-mode", "Enter visual mode", [](auto& ctx, auto& args) {
    ctx.modes().enter(Mode::VISUAL);
    ctx.set_status_message("VISUAL mode");
    return true;
  });

  command_manager_.register_command("enter-command-mode", "Enter command mode", [](auto& ctx, auto& args) {
    ctx.modes().enter(Mode::COMMAND);
    ctx.set_status_message("COMMAND mode");
    return true;
  });

  command_manager_.register_command("command-mode", "Enter command mode", [](auto& ctx, auto& args) {
    ctx.modes().enter(Mode::COMMAND);
    ctx.set_status_message("COMMAND mode");
    return true;
  });

  command_manager_.register_command("copy-mode", "Enter copy/visual mode", [](auto& ctx, auto& args) {
    ctx.modes().enter(Mode::VISUAL);
    ctx.set_status_message("VISUAL mode");
    return true;
  });

  // Application commands
  command_manager_.register_command("quit", "Quit application", [](auto& ctx, auto& args) {
    ctx.set_quit_flag();
    if (ctx.screen().ExitLoopClosure()) {
      ctx.screen().ExitLoopClosure()();
    }
    return true;
  });

  // Alias :q for :quit
  command_manager_.register_command("q", "Quit application (alias for :quit)", [](auto& ctx, auto& args) {
    ctx.set_quit_flag();
    if (ctx.screen().ExitLoopClosure()) {
      ctx.screen().ExitLoopClosure()();
    }
    return true;
  });

  command_manager_.register_command("help", "Show help", [](auto& ctx, auto& args) {
    std::string help = ctx.keys().help_text() + "\n" + ctx.commands().help_text();
    ctx.set_status_message("See logs for help text");
    ctx.log_info("=== HELP ===\n" + help);
    return true;
  });

  // Register menu navigation commands
  command_manager_.register_command("load", "Load a menu", [](auto& ctx, auto& args) {
    if (args.empty()) {
      ctx.set_status_message("Usage: :load <menu-id>");
      ctx.log_warn("[load] No menu ID provided");
      return false;
    }

    if (!ctx.menus().is_registered(args[0])) {
      ctx.set_status_message("Unknown menu: " + args[0]);
      ctx.log_error("[load] Menu not found: " + args[0]);
      return false;
    }

    if (!ctx.menus().load(args[0])) {
      ctx.set_status_message("Failed to load: " + args[0]);
      return false;
    }

    ctx.set_status_message("Loaded: " + args[0]);
    return true;
  });

  command_manager_.register_command("back", "Go back to previous menu", [](auto& ctx, auto& args) {
    if (!ctx.menus().back()) {
      ctx.set_status_message("Cannot go back");
      return false;
    }
    ctx.set_status_message("Back");
    return true;
  });

  command_manager_.register_command("forward", "Go forward to next menu", [](auto& ctx, auto& args) {
    if (!ctx.menus().forward()) {
      ctx.set_status_message("Cannot go forward");
      return false;
    }
    ctx.set_status_message("Forward");
    return true;
  });

  command_manager_.register_command("menus", "List all registered menus", [](auto& ctx, auto& args) {
    auto list = ctx.menus().list_registered();
    std::string msg = "Registered menus: ";
    for (size_t i = 0; i < list.size(); ++i) {
      if (i > 0) msg += ", ";
      msg += list[i];
    }
    ctx.set_status_message(msg);
    ctx.log_info("[menus] " + msg);
    return true;
  });

  command_manager_.register_command("home", "Exit menu and return to welcome screen", [](auto& ctx, auto& args) {
    ctx.menus().clear_stack();
    ctx.set_status_message("Welcome");
    return true;
  });

  log_info("TUIContext initialized with built-in commands");
}

Window* TUIContext::active_window() {
  std::lock_guard<std::mutex> lock(windows_mutex_);
  if (windows_.empty() || active_window_index_ < 0 ||
      active_window_index_ >= static_cast<int>(windows_.size())) {
    return nullptr;
  }
  return windows_[active_window_index_].get();
}

const Window* TUIContext::active_window() const {
  std::lock_guard<std::mutex> lock(windows_mutex_);
  if (windows_.empty() || active_window_index_ < 0 ||
      active_window_index_ >= static_cast<int>(windows_.size())) {
    return nullptr;
  }
  return windows_[active_window_index_].get();
}

std::vector<Window*> TUIContext::all_windows() {
  std::lock_guard<std::mutex> lock(windows_mutex_);
  std::vector<Window*> result;
  result.reserve(windows_.size());
  for (auto& win : windows_) {
    result.push_back(win.get());
  }
  return result;
}

std::vector<const Window*> TUIContext::all_windows() const {
  std::lock_guard<std::mutex> lock(windows_mutex_);
  std::vector<const Window*> result;
  result.reserve(windows_.size());
  for (const auto& win : windows_) {
    result.push_back(win.get());
  }
  return result;
}

Window* TUIContext::create_window(const std::string& shell) {
  std::lock_guard<std::mutex> lock(windows_mutex_);

  std::string shell_cmd = shell.empty() ? default_shell_ : shell;
  auto new_window = std::make_unique<Window>(shell_cmd, log_);

  if (!new_window) {
    log_error("Failed to create window");
    return nullptr;
  }

  const int new_index = static_cast<int>(windows_.size());
  new_window->set_name("win-" + std::to_string(new_index + 1));

  Window* result = new_window.get();
  windows_.push_back(std::move(new_window));

  last_window_index_ = active_window_index_;
  active_window_index_ = static_cast<int>(windows_.size()) - 1;

  log_info("Created window: " + result->name());
  return result;
}

bool TUIContext::close_window(Window* window) {
  std::lock_guard<std::mutex> lock(windows_mutex_);

  if (windows_.size() <= 1) {
    log_warn("Cannot close last window");
    return false;
  }

  auto it = std::find_if(windows_.begin(), windows_.end(),
                         [window](const auto& w) { return w.get() == window; });

  if (it == windows_.end()) {
    log_warn("Window not found");
    return false;
  }

  int index = static_cast<int>(std::distance(windows_.begin(), it));
  windows_.erase(it);

  // Adjust active index
  if (active_window_index_ >= static_cast<int>(windows_.size())) {
    active_window_index_ = static_cast<int>(windows_.size()) - 1;
  }
  if (active_window_index_ < 0) {
    active_window_index_ = 0;
  }

  // Adjust last index
  if (last_window_index_ >= static_cast<int>(windows_.size())) {
    last_window_index_ = static_cast<int>(windows_.size()) - 1;
  }
  if (last_window_index_ < 0) {
    last_window_index_ = 0;
  }

  // Rename remaining windows
  for (size_t i = 0; i < windows_.size(); ++i) {
    if (windows_[i]) {
      windows_[i]->set_name("win-" + std::to_string(i + 1));
    }
  }

  log_info("Closed window");
  return true;
}

bool TUIContext::close_active_window() {
  Window* win = active_window();
  if (!win) {
    return false;
  }
  return close_window(win);
}

bool TUIContext::focus_window(int index) {
  std::lock_guard<std::mutex> lock(windows_mutex_);

  if (index < 0 || index >= static_cast<int>(windows_.size())) {
    return false;
  }

  if (active_window_index_ == index) {
    return true;
  }

  last_window_index_ = active_window_index_;
  active_window_index_ = index;

  log_info("Focused window " + std::to_string(index));
  return true;
}

bool TUIContext::focus_next_window() {
  std::lock_guard<std::mutex> lock(windows_mutex_);

  if (windows_.size() <= 1) {
    return false;
  }

  last_window_index_ = active_window_index_;
  active_window_index_ = (active_window_index_ + 1) % static_cast<int>(windows_.size());

  log_info("Focused next window");
  return true;
}

bool TUIContext::focus_previous_window() {
  std::lock_guard<std::mutex> lock(windows_mutex_);

  if (windows_.size() <= 1) {
    return false;
  }

  last_window_index_ = active_window_index_;
  active_window_index_ = active_window_index_ - 1;
  if (active_window_index_ < 0) {
    active_window_index_ = static_cast<int>(windows_.size()) - 1;
  }

  log_info("Focused previous window");
  return true;
}

bool TUIContext::focus_last_window() {
  std::lock_guard<std::mutex> lock(windows_mutex_);

  if (windows_.size() <= 1 || last_window_index_ == active_window_index_) {
    return false;
  }

  int max_index = static_cast<int>(windows_.size()) - 1;
  last_window_index_ = std::clamp(last_window_index_, 0, max_index);

  std::swap(last_window_index_, active_window_index_);

  log_info("Focused last window");
  return true;
}

size_t TUIContext::window_count() const {
  std::lock_guard<std::mutex> lock(windows_mutex_);
  return windows_.size();
}

int TUIContext::active_window_index() const {
  std::lock_guard<std::mutex> lock(windows_mutex_);
  return active_window_index_;
}

void TUIContext::set_status_message(const std::string& msg) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  status_message_ = msg;
}

std::string TUIContext::get_status_message() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return status_message_;
}

std::string TUIContext::get_default_shell() const {
  return default_shell_;
}

void TUIContext::log_info(const std::string& msg) const {
  if (log_) {
    log_->info("TUI", msg);
  }
}

void TUIContext::log_debug(const std::string& msg) const {
  if (log_) {
    log_->debug("TUI", msg);
  }
}

void TUIContext::log_warn(const std::string& msg) const {
  if (log_) {
    log_->warn("TUI", msg);
  }
}

void TUIContext::log_error(const std::string& msg) const {
  if (log_) {
    log_->error("TUI", msg);
  }
}

Menu* TUIContext::active_menu() {
  return menu_manager_.current();
}

const Menu* TUIContext::active_menu() const {
  return menu_manager_.current();
}

ComponentBase* TUIContext::focused_component() {
  std::lock_guard<std::mutex> lock(focus_mutex_);
  return focused_component_;
}

const ComponentBase* TUIContext::focused_component() const {
  std::lock_guard<std::mutex> lock(focus_mutex_);
  return focused_component_;
}

bool TUIContext::focus_next_component() {
  // TODO: Implement sequential focus traversal
  // For now, just a stub
  log_info("[focus] focus_next_component() not yet implemented");
  return false;
}

bool TUIContext::focus_previous_component() {
  // TODO: Implement sequential focus traversal
  // For now, just a stub
  log_info("[focus] focus_previous_component() not yet implemented");
  return false;
}

bool TUIContext::focus_component_left() {
  // TODO: Implement spatial focus navigation
  // For now, just a stub
  log_info("[focus] focus_component_left() not yet implemented");
  return false;
}

bool TUIContext::focus_component_right() {
  // TODO: Implement spatial focus navigation
  // For now, just a stub
  log_info("[focus] focus_component_right() not yet implemented");
  return false;
}

bool TUIContext::focus_component_up() {
  // TODO: Implement spatial focus navigation
  // For now, just a stub
  log_info("[focus] focus_component_up() not yet implemented");
  return false;
}

bool TUIContext::focus_component_down() {
  // TODO: Implement spatial focus navigation
  // For now, just a stub
  log_info("[focus] focus_component_down() not yet implemented");
  return false;
}

bool TUIContext::insert_mode_enabled() const {
  auto* menu = active_menu();
  return menu && menu->supports_insert_mode() && modes().current() == Mode::INSERT;
}

} // namespace nazg::tui

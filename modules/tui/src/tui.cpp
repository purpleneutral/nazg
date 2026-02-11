// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 purpleneutral
//
// This file is part of nazg.
//
// nazg is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// nazg is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along
// with nazg. If not, see <https://www.gnu.org/licenses/>.

#include "tui/tui.hpp"
#include "tui/theme.hpp"
#include "tui/layout_utils.hpp"
#include "tui/menu_registry.hpp"
#include "blackbox/logger.hpp"
#include "config/config.hpp"
#include "nexus/store.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <algorithm>
#include <chrono>
#include <thread>
#include <termios.h>
#include <unistd.h>

namespace nazg::tui {

using namespace ftxui;
using namespace std::chrono_literals;

namespace {

class FlowControlGuard {
public:
  FlowControlGuard() {
    if (tcgetattr(STDIN_FILENO, &original_) != 0) {
      return;
    }

    termios modified = original_;
    if ((modified.c_iflag & (IXON | IXOFF | IXANY)) == 0) {
      return;
    }

    modified.c_iflag &= ~(IXON | IXOFF | IXANY);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &modified) == 0) {
      active_ = true;
    }
  }

  ~FlowControlGuard() {
    if (active_) {
      tcsetattr(STDIN_FILENO, TCSANOW, &original_);
    }
  }

private:
  termios original_{};
  bool active_ = false;
};

} // namespace

TUIApp::TUIApp(nazg::blackbox::logger* log, nazg::config::store* cfg)
    : ctx_(std::make_unique<TUIContext>(log, cfg)),
      screen_(ScreenInteractive::Fullscreen()),
      theme_(cyberpunk_theme()) {

  statusbar_.set_prefix_hint("Ctrl-b");

  // Load theme from config if specified
  if (cfg) {
    std::string theme_name = cfg->get_string("tui", "theme", "cyberpunk");
    theme_ = get_theme(theme_name);
    ctx_->log_info("Using theme: " + theme_.name);
  }

  // Set screen reference in context
  ctx_->set_screen(&screen_);
}

void TUIApp::register_menus(nazg::nexus::Store* store) {
  if (!store) {
    ctx_->log_warn("Cannot register menus: store is null");
    return;
  }

  register_builtin_menus(*ctx_, store, ctx_->logger());
  ctx_->log_info("Built-in menus registered");
}

int TUIApp::run() {
  FlowControlGuard flow_guard;

  // Enable TUI mode on logger to suppress console output (would corrupt TUI display)
  // File logging continues to work
  if (auto* log = ctx_->logger()) {
    log->enable_tui_mode(true);
  }

  ctx_->log_info("Starting TUI application");

  // Initialize context (registers commands, etc.)
  ctx_->initialize();

  // Start with no window - just show welcome screen
  // User can create windows with :new-window or load menus with :load

  // Create component
  auto component = make_component();

  // Start update thread for reading terminal output
  std::thread update_thread([this]() {
    while (!ctx_->should_quit()) {
      update_loop();
      std::this_thread::sleep_for(16ms); // ~60 FPS
    }
  });

  // Run the UI loop
  screen_.Loop(component);

  // Cleanup
  if (update_thread.joinable()) {
    update_thread.join();
  }

  ctx_->log_info("TUI application exited");

  // Disable TUI mode on logger when exiting
  if (auto* log = ctx_->logger()) {
    log->enable_tui_mode(false);
  }

  return 0;
}

Component TUIApp::make_component() {
  return Renderer([this] { return render(); }) |
         CatchEvent([this](Event event) { return handle_input(event); });
}

bool TUIApp::handle_input(Event event) {
  const bool is_custom_event = (event == Event::Custom);

  // Check prefix timeout
  ctx_->modes().check_prefix_timeout();

  // Ignore custom refresh events early
  if (is_custom_event) {
    return false;
  }

  // Command bar has priority when active
  if (ctx_->command_bar().active()) {
    return ctx_->command_bar().handle_event(event, *ctx_);
  }

  // Get current mode
  Mode current_mode = ctx_->modes().current();

  // Check for prefix key (Ctrl-B)
  auto activate_prefix = [&]() {
    ctx_->modes().activate_prefix(1000);
    ctx_->set_status_message("PREFIX MODE");
    ctx_->log_info("PREFIX MODE ACTIVATED");
  };

  // Method 1: Character event with code 2
  if (event.is_character()) {
    const std::string& input = event.character();
    if (!input.empty() && static_cast<unsigned char>(input[0]) == 2) {
      activate_prefix();
      return true;
    }
  }

  // Method 2: Direct string comparison
  if (!event.input().empty() &&
      static_cast<unsigned char>(event.input()[0]) == 2) {
    activate_prefix();
    return true;
  }

  // Alt-B - Also activate prefix mode (Alt sends ESC + character)
  if (event == Event::Escape) {
    saw_escape_ = true;
    return true;
  }

  // Check if previous event was escape and this is 'b' (Alt+B)
  if (saw_escape_) {
    if (event.is_character() && event.character() == "b") {
      saw_escape_ = false;
      activate_prefix();
      return true;
    }
    // Not Alt+B, send the escape to shell if in INSERT mode
    saw_escape_ = false;
    if (current_mode == Mode::INSERT) {
      Window* win = ctx_->active_window();
      if (win) {
        win->send_input("\x1b");
      }
    }
    // Fall through to process the current character
  }

  // Handle based on current mode
  if (!ctx_->command_bar().active() && event.is_character() &&
      event.character() == ":") {
    ctx_->command_bar().activate();
    ctx_->set_status_message("Command mode");
    ctx_->log_info("CommandBar activated");
    return true;
  }

  if (current_mode == Mode::PREFIX) {
    return handle_prefix_input(event);
  } else if (current_mode == Mode::INSERT) {
    return handle_insert_input(event);
  } else if (current_mode == Mode::NORMAL) {
    // If there's an active menu, let it handle events first
    Menu* menu = ctx_->active_menu();
    if (menu && menu->root()) {
      if (menu->root()->handle_event(event)) {
        return true;
      }
    }

    // In NORMAL mode, check for keybindings
    auto cmd = ctx_->keys().lookup(event, Mode::NORMAL, false);
    if (cmd) {
      bool success = ctx_->commands().execute(*ctx_, *cmd);
      if (!success) {
        ctx_->set_status_message("Command failed: " + *cmd);
      }
      return true;
    }
  }

  return false;
}

bool TUIApp::handle_prefix_input(Event event) {
  saw_escape_ = false;

  // Ignore refresh events
  if (event == Event::Custom) {
    return false;
  }

  // Log the event for debugging
  std::string key_str = KeyManager::event_to_key_string(event);
  ctx_->log_info("Prefix input received: key='" + key_str + "'");

  if (!key_str.empty()) {
    auto direct = ctx_->keys().lookup_by_key(key_str, Mode::PREFIX, true);
    ctx_->log_debug("Direct lookup for '" + key_str + "': " +
                    (direct ? *direct : "<none>"));
  }

  // Look up command for this key
  auto cmd = ctx_->keys().lookup(event, Mode::PREFIX, true);
  if (cmd) {
    ctx_->log_info("Found command for prefix key: " + *cmd);

    // Deactivate prefix before executing command
    ctx_->modes().deactivate_prefix();

    // Execute command
    ctx_->log_info("Executing command: " + *cmd);
    bool success = ctx_->commands().execute(*ctx_, *cmd);
    if (!success) {
      ctx_->set_status_message("Command failed: " + *cmd);
      ctx_->log_warn("Command failed: " + *cmd);
    } else {
      ctx_->log_info("Command succeeded: " + *cmd);
    }
    return true;
  }

  // Unknown prefix command - deactivate prefix
  ctx_->modes().deactivate_prefix();

  // Swallow benign filler events
  const std::string& raw = event.input();
  if (raw.empty() || (raw.size() == 1 && raw[0] == '\0')) {
    return true;
  }

  ctx_->set_status_message("Unknown prefix command");
  ctx_->log_warn("Unknown prefix command: " + key_str);
  return false;
}

bool TUIApp::handle_insert_input(Event event) {
  Window* win = ctx_->active_window();
  if (!win) {
    return false;
  }

  // Regular characters
  if (event.is_character()) {
    // Ctrl-D to exit
    if (event.character() == "\x04") {
      ctx_->set_quit_flag();
      screen_.ExitLoopClosure()();
      return true;
    }
    win->send_input(event.character());
    return true;
  }

  // Special keys
  if (event == Event::ArrowUp) {
    win->send_input("\x1b[A");
    return true;
  }
  if (event == Event::ArrowDown) {
    win->send_input("\x1b[B");
    return true;
  }
  if (event == Event::ArrowLeft) {
    win->send_input("\x1b[D");
    return true;
  }
  if (event == Event::ArrowRight) {
    win->send_input("\x1b[C");
    return true;
  }
  if (event == Event::Backspace) {
    win->send_input("\x7f");
    return true;
  }
  if (event == Event::Delete) {
    win->send_input("\x1b[3~");
    return true;
  }
  if (event == Event::Home) {
    win->send_input("\x1b[H");
    return true;
  }
  if (event == Event::End) {
    win->send_input("\x1b[F");
    return true;
  }
  if (event == Event::PageUp) {
    win->send_input("\x1b[5~");
    return true;
  }
  if (event == Event::PageDown) {
    win->send_input("\x1b[6~");
    return true;
  }
  if (event == Event::Tab) {
    win->send_input("\t");
    return true;
  }
  if (event == Event::Return) {
    win->send_input("\r");
    return true;
  }
  if (event == Event::F7) {
    ctx_->set_quit_flag();
    screen_.ExitLoopClosure()();
    return true;
  }

  return false;
}

Element TUIApp::render() {
  // Get terminal size
  int width = screen_.dimx();
  int height = screen_.dimy();

  Element main_content;
  const auto& frame_opts = default_menu_frame_options();

  // Check if there's an active menu - if so, render it instead of window
  Menu* menu = ctx_->active_menu();
  if (menu && menu->root()) {
    const int available_width =
        std::max(0, width - horizontal_overhead(frame_opts));
    const int available_height =
        std::max(0, (height - 1) - vertical_overhead(frame_opts));

    Element menu_inner =
        menu->root()->render(available_width, available_height, theme_);
    main_content = apply_frame(std::move(menu_inner), width, height - 1,
                               theme_, frame_opts);
  } else {
    // Render window as usual
    Window* window = ctx_->active_window();
    if (window) {
      main_content = window->render(width, height - 1, theme_);
    } else {
      // Render welcome screen
      main_content = vbox({
        text("") | flex,
        text("") | flex,
        text("╔═══════════════════════════════════════╗") | center | color(Color::Cyan),
        text("║                                       ║") | center | color(Color::Cyan),
        text("║         NAZG TUI Multiplexer          ║") | center | color(Color::Cyan) | bold,
        text("║              Version 1.0              ║") | center | color(Color::Cyan),
        text("║                                       ║") | center | color(Color::Cyan),
        text("╚═══════════════════════════════════════╝") | center | color(Color::Cyan),
        text("") | flex,
        text("Available commands:") | center | bold,
        text("") | flex,
        text(":load main    - Open main menu") | center,
        text(":load docker  - Docker management") | center,
        text(":menus        - List all menus") | center,
        text(":new-window   - Create shell window") | center,
        text(":help         - Show help") | center,
        text(":q            - Quit") | center,
        text("") | flex,
        text("") | flex,
      });
    }
  }

  // Update and render status bar
  update_status_bar();
  Element command_bar = ctx_->command_bar().render(width);
  Element status_bar = statusbar_.render(width, theme_);

  return vbox({
      main_content | flex,
      command_bar,
      status_bar,
  });
}

void TUIApp::update_loop() {
  auto windows = ctx_->all_windows();
  for (auto* window : windows) {
    if (window) {
      window->update();
    }
  }

  // Request screen refresh
  screen_.PostEvent(Event::Custom);
}

void TUIApp::update_status_bar() {
  // Set prefix state
  statusbar_.set_prefix_active(ctx_->modes().is_prefix_active());

  // Set process info for active window
  Window* window = ctx_->active_window();
  if (window) {
    pid_t pid = 0;
    bool alive = false;
    if (window->active_process_info(pid, alive)) {
      statusbar_.set_process_info(pid, alive);
    } else {
      statusbar_.set_process_info(0, false);
    }

    // Set window summary
    int index = ctx_->active_window_index();
    int total = static_cast<int>(ctx_->window_count());
    bool zoomed = window->is_zoomed();
    std::string name = window->name();
    statusbar_.set_window_summary(index, total, zoomed, name);
  } else {
    statusbar_.set_process_info(0, false);
    statusbar_.set_window_summary(0, 0, false, "");
  }

  // Set status message
  std::string msg = ctx_->get_status_message();
  if (!msg.empty()) {
    statusbar_.set_info(msg);
  }
}

} // namespace nazg::tui

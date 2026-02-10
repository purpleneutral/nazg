#include "tui/managers/key_manager.hpp"
#include <cctype>
#include <sstream>

namespace nazg::tui {

using namespace ftxui;

KeyManager::KeyManager() {
  load_defaults();
}

bool KeyManager::bind(const KeyBinding& binding) {
  auto key = make_key(binding.mode, binding.is_prefix);
  bindings_[key][binding.key] = binding;
  return true;
}

bool KeyManager::unbind(const std::string& key, Mode mode, bool is_prefix) {
  auto map_key = make_key(mode, is_prefix);
  auto it = bindings_.find(map_key);
  if (it == bindings_.end()) {
    return false;
  }
  return it->second.erase(key) > 0;
}

std::optional<std::string> KeyManager::lookup(const Event& event, Mode mode,
                                               bool is_prefix) const {
  std::string key_str = event_to_key_string(event);
  if (!key_str.empty()) {
    if (auto found = lookup_by_key(key_str, mode, is_prefix)) {
      return found;
    }
  }

  if (event.is_character()) {
    const std::string& ch = event.character();
    if (!ch.empty()) {
      unsigned char c = static_cast<unsigned char>(ch[0]);
      unsigned char lower = static_cast<unsigned char>(std::tolower(c));
      if (lower != c) {
        std::string alt(1, static_cast<char>(lower));
        if (auto found = lookup_by_key(alt, mode, is_prefix)) {
          return found;
        }
      }
    }
  }

  return std::nullopt;
}

std::optional<std::string> KeyManager::lookup_by_key(const std::string& key,
                                                      Mode mode,
                                                      bool is_prefix) const {
  auto map_key = make_key(mode, is_prefix);
  auto it = bindings_.find(map_key);
  if (it == bindings_.end()) {
    return std::nullopt;
  }

  auto binding_it = it->second.find(key);
  if (binding_it == it->second.end()) {
    return std::nullopt;
  }

  return binding_it->second.command;
}

std::vector<KeyBinding> KeyManager::get_bindings(Mode mode,
                                                  bool is_prefix) const {
  std::vector<KeyBinding> result;
  auto map_key = make_key(mode, is_prefix);
  auto it = bindings_.find(map_key);
  if (it != bindings_.end()) {
    for (const auto& [key, binding] : it->second) {
      result.push_back(binding);
    }
  }
  return result;
}

void KeyManager::load_defaults() {
  // Prefix bindings (require Ctrl-B first)
  bind({"c", "new-window", "Create new window"});
  bind({"|", "split-vertical", "Split pane vertically"});
  bind({"_", "split-horizontal", "Split pane horizontally"});
  bind({"x", "kill-pane", "Close current pane"});
  bind({"d", "kill-pane", "Close current pane"});
  bind({"X", "kill-window", "Close current window"});
  bind({"&", "kill-window", "Close current window"});
  bind({"h", "navigate-left", "Navigate to pane on left"});
  bind({"j", "navigate-down", "Navigate to pane below"});
  bind({"k", "navigate-up", "Navigate to pane above"});
  bind({"l", "navigate-right", "Navigate to pane on right"});
  bind({"n", "next-window", "Switch to next window"});
  bind({"p", "previous-window", "Switch to previous window"});
  bind({"o", "last-window", "Switch to last window"});
  bind({"+", "toggle-zoom", "Toggle pane zoom"});
  bind({"z", "toggle-zoom", "Toggle pane zoom"});
  bind({"q", "quit", "Quit application"});
  bind({"?", "help", "Show help"});
  bind({"Ctrl-S", "save-layout", "Save layout"});
  bind({"Ctrl-R", "restore-layout", "Restore layout"});
  bind({":", "command-mode", "Enter command mode"});
  bind({"[", "copy-mode", "Enter copy/visual mode"});
  bind({"r", "rename-window", "Rename current window"});

  // Arrow key navigation (with prefix)
  bind({"ArrowLeft", "navigate-left", "Navigate to pane on left"});
  bind({"ArrowRight", "navigate-right", "Navigate to pane on right"});
  bind({"ArrowUp", "navigate-up", "Navigate to pane above"});
  bind({"ArrowDown", "navigate-down", "Navigate to pane below"});

  // Normal mode bindings (no prefix needed)
  bind({"i", "enter-insert-mode", "Enter insert mode", Mode::NORMAL, false});
  bind({"v", "enter-visual-mode", "Enter visual mode", Mode::NORMAL, false});
  bind({":", "enter-command-mode", "Enter command mode", Mode::NORMAL, false});
  bind({"Escape", "enter-normal-mode", "Enter normal mode", Mode::NORMAL, false});

  // Global bindings (work in any mode except INSERT)
  bind({"F7", "quit", "Quit application", Mode::NORMAL, false});
  bind({"Ctrl-D", "quit", "Quit application", Mode::NORMAL, false});
}

void KeyManager::clear() {
  bindings_.clear();
}

std::string KeyManager::help_text() const {
  std::ostringstream oss;
  oss << "=== Key Bindings ===\n\n";

  oss << "Prefix Commands (Ctrl-B + key):\n";
  auto prefix_bindings = get_bindings(Mode::PREFIX, true);
  for (const auto& binding : prefix_bindings) {
    oss << "  " << binding.key << " - " << binding.description << "\n";
  }

  oss << "\nNormal Mode:\n";
  auto normal_bindings = get_bindings(Mode::NORMAL, false);
  for (const auto& binding : normal_bindings) {
    oss << "  " << binding.key << " - " << binding.description << "\n";
  }

  return oss.str();
}

std::string KeyManager::event_to_key_string(const Event& event) {
  // Handle special keys
  if (event == Event::ArrowLeft) return "ArrowLeft";
  if (event == Event::ArrowRight) return "ArrowRight";
  if (event == Event::ArrowUp) return "ArrowUp";
  if (event == Event::ArrowDown) return "ArrowDown";
  if (event == Event::Backspace) return "Backspace";
  if (event == Event::Delete) return "Delete";
  if (event == Event::Home) return "Home";
  if (event == Event::End) return "End";
  if (event == Event::PageUp) return "PageUp";
  if (event == Event::PageDown) return "PageDown";
  if (event == Event::Tab) return "Tab";
  if (event == Event::TabReverse) return "Shift-Tab";
  if (event == Event::Return) return "Return";
  if (event == Event::Escape) return "Escape";
  if (event == Event::F1) return "F1";
  if (event == Event::F2) return "F2";
  if (event == Event::F3) return "F3";
  if (event == Event::F4) return "F4";
  if (event == Event::F5) return "F5";
  if (event == Event::F6) return "F6";
  if (event == Event::F7) return "F7";
  if (event == Event::F8) return "F8";
  if (event == Event::F9) return "F9";
  if (event == Event::F10) return "F10";
  if (event == Event::F11) return "F11";
  if (event == Event::F12) return "F12";

  // Character events
  if (event.is_character()) {
    std::string ch = event.character();
    if (ch.empty()) return "";

    // Handle control characters
    unsigned char code = static_cast<unsigned char>(ch[0]);
    if (code == 2) return "Ctrl-B";
    if (code == 3) return "Ctrl-C";
    if (code == 4) return "Ctrl-D";
    if (code < 32) {
      return "Ctrl-" + std::string(1, 'A' + (code - 1));
    }

    // Regular character
    return ch;
  }

  // Unknown
  return "";
}

} // namespace nazg::tui

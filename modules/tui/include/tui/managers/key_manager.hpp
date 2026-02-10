#pragma once

#include "tui/managers/mode_manager.hpp"
#include <ftxui/component/event.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nazg::tui {

/**
 * @brief Key binding entry
 */
struct KeyBinding {
  std::string key;              // Key sequence (e.g., "c", "x", "|", "Ctrl-C")
  std::string command;          // Command name to execute
  std::string description;      // Human-readable description
  Mode mode = Mode::PREFIX;     // Mode in which this binding is active
  bool is_prefix = true;        // Whether this requires prefix key first

  KeyBinding() = default;
  KeyBinding(const std::string& k, const std::string& cmd, const std::string& desc,
             Mode m = Mode::PREFIX, bool prefix = true)
      : key(k), command(cmd), description(desc), mode(m), is_prefix(prefix) {}
};

/**
 * @brief KeyManager manages all keybindings
 *
 * This manager provides:
 * - Keybinding registration API
 * - Key lookup for command execution
 * - Default keybindings
 * - Per-mode keybindings
 * - External module API for registering bindings
 */
class KeyManager {
public:
  KeyManager();

  /**
   * @brief Register a keybinding
   * @param binding The binding to register
   * @return true if registered successfully
   */
  bool bind(const KeyBinding& binding);

  /**
   * @brief Unregister a keybinding
   * @param key Key sequence
   * @param mode Mode for the binding
   * @param is_prefix Whether binding is prefix-based
   * @return true if unbound successfully
   */
  bool unbind(const std::string& key, Mode mode, bool is_prefix);

  /**
   * @brief Look up command for a key event
   * @param event The FTXUI event
   * @param mode Current mode
   * @param is_prefix Whether prefix is active
   * @return Command name if found
   */
  std::optional<std::string> lookup(const ftxui::Event& event, Mode mode,
                                    bool is_prefix) const;

  /**
   * @brief Look up command by key string
   * @param key Key string (e.g., "c", "|", "Ctrl-C")
   * @param mode Current mode
   * @param is_prefix Whether prefix is active
   * @return Command name if found
   */
  std::optional<std::string> lookup_by_key(const std::string& key, Mode mode,
                                           bool is_prefix) const;

  /**
   * @brief Get all bindings for a mode
   * @param mode Mode to query
   * @param is_prefix Filter for prefix bindings
   * @return Vector of bindings
   */
  std::vector<KeyBinding> get_bindings(Mode mode, bool is_prefix) const;

  /**
   * @brief Load default keybindings (tmux-like)
   */
  void load_defaults();

  /**
   * @brief Clear all bindings
   */
  void clear();

  /**
   * @brief Get help text for all bindings
   */
  std::string help_text() const;

  /**
   * @brief Convert FTXUI Event to key string
   */
  static std::string event_to_key_string(const ftxui::Event& event);

private:
  // Map: (mode, is_prefix) -> key -> binding
  std::map<std::pair<Mode, bool>, std::map<std::string, KeyBinding>> bindings_;

  /**
   * @brief Helper to create binding map key
   */
  static std::pair<Mode, bool> make_key(Mode mode, bool is_prefix) {
    return {mode, is_prefix};
  }
};

} // namespace nazg::tui

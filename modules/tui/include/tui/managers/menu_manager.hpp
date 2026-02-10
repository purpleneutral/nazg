#pragma once

#include "tui/menu.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::tui {

// Forward declaration
class TUIContext;

/**
 * @brief Manages menu registration and navigation
 *
 * The MenuManager handles:
 * - Registration of menus from built-in and module sources
 * - Navigation stack for menu history (:back/:forward)
 * - Menu lifecycle (load, unload, resume)
 * - State preservation across navigation
 *
 * Usage:
 * ```cpp
 * // Registration (in module init or TUIContext::initialize)
 * REGISTER_MENU(ctx, MainMenu);
 * REGISTER_MENU(ctx, GitStatusMenu);
 *
 * // Loading menus
 * ctx.menus().load("MainMenu");          // Push onto stack
 * ctx.menus().back();                    // Pop, restore previous
 * ctx.menus().forward();                 // Re-push if available
 *
 * // Query
 * auto* current = ctx.menus().current(); // Get top of stack
 * auto list = ctx.menus().list_registered();
 * ```
 */
class MenuManager {
public:
  /**
   * @brief Factory function for creating menu instances
   */
  using MenuFactory = std::function<std::unique_ptr<Menu>()>;

  /**
   * @brief Create menu manager
   * @param log Logger instance (optional)
   */
  explicit MenuManager(nazg::blackbox::logger* log = nullptr);

  /**
   * @brief Set TUI context (required for building menus)
   * @param ctx TUI context pointer
   */
  void set_context(TUIContext* ctx);

  // ============================================================================
  // Registration
  // ============================================================================

  /**
   * @brief Register a menu factory
   * @param id Unique menu identifier (used for :load command)
   * @param factory Factory function that creates menu instances
   */
  void register_menu(const std::string& id, MenuFactory factory);

  /**
   * @brief Check if a menu is registered
   * @param id Menu identifier
   * @return true if menu exists
   */
  bool is_registered(const std::string& id) const;

  /**
   * @brief Get list of all registered menu IDs
   * @return Vector of menu identifiers
   */
  std::vector<std::string> list_registered() const;

  // ============================================================================
  // Navigation
  // ============================================================================

  /**
   * @brief Load a menu (push onto navigation stack)
   *
   * This creates a new instance of the menu, builds its component tree,
   * and pushes it onto the navigation stack. The previous menu (if any)
   * has its state saved if preserve_state() is true.
   *
   * @param menu_id Menu identifier
   * @return true if menu was loaded successfully
   */
  bool load(const std::string& menu_id);

  /**
   * @brief Go back to previous menu (pop current from stack)
   *
   * Saves state of current menu (if preserve_state() is true), pops it
   * from the stack, and restores the previous menu's state. The current
   * menu is added to the forward stack for potential :forward.
   *
   * @return true if there was a previous menu to return to
   */
  bool back();

  /**
   * @brief Go forward to next menu (re-push from forward stack)
   *
   * Only works after :back has been called. Re-pushes the menu that was
   * previously popped.
   *
   * @return true if there was a menu in the forward stack
   */
  bool forward();

  /**
   * @brief Get current menu (top of stack)
   * @return Current menu, or nullptr if stack is empty
   */
  Menu* current() const;

  /**
   * @brief Get stack depth (number of menus in navigation history)
   * @return Number of menus on the stack
   */
  size_t stack_depth() const;

  /**
   * @brief Clear the navigation stack
   *
   * Calls on_unload() for all menus and clears both navigation and
   * forward stacks.
   */
  void clear_stack();

private:
  /**
   * @brief Stack entry with menu and saved state
   */
  struct MenuStackEntry {
    std::unique_ptr<Menu> menu;
    Menu::MenuState saved_state;
    std::string menu_id;  // For forward stack restoration
  };

  // Registry of menu factories
  std::map<std::string, MenuFactory> factories_;

  // Navigation stack (top = current menu)
  std::vector<MenuStackEntry> stack_;

  // Forward stack (for :forward after :back)
  std::vector<std::string> forward_stack_;

  // Logger
  nazg::blackbox::logger* log_ = nullptr;

  // TUI Context (for building menus)
  TUIContext* ctx_ = nullptr;

  // Helpers
  void log_info(const std::string& msg) const;
  void log_warn(const std::string& msg) const;
  void log_error(const std::string& msg) const;

  /**
   * @brief Ensure TUI mode matches whether a menu is active
   */
  void update_mode_for_active_menu();
};

// ============================================================================
// Convenience Macro
// ============================================================================

/**
 * @brief Register a menu with the MenuManager
 *
 * Usage:
 * ```cpp
 * REGISTER_MENU(ctx, MainMenu);
 * REGISTER_MENU(ctx, GitStatusMenu);
 * ```
 *
 * Expands to:
 * ```cpp
 * ctx.menus().register_menu("MainMenu", []() {
 *   return std::make_unique<MainMenu>();
 * });
 * ```
 */
#define REGISTER_MENU(ctx, MenuClass) \
  (ctx).menus().register_menu(#MenuClass, []() { \
    return std::make_unique<MenuClass>(); \
  })

} // namespace nazg::tui

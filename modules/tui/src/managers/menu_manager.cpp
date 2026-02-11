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

#include "tui/managers/menu_manager.hpp"
#include "tui/tui_context.hpp"
#include "blackbox/logger.hpp"

namespace nazg::tui {

MenuManager::MenuManager(nazg::blackbox::logger* log) : log_(log) {
  log_info("MenuManager initialized");
}

void MenuManager::set_context(TUIContext* ctx) {
  ctx_ = ctx;
  log_info("TUIContext set for MenuManager");
}

void MenuManager::register_menu(const std::string& id, MenuFactory factory) {
  if (factories_.find(id) != factories_.end()) {
    log_warn("Menu '" + id + "' already registered, overwriting");
  }

  factories_[id] = factory;
  log_info("Registered menu: " + id);
}

bool MenuManager::is_registered(const std::string& id) const {
  return factories_.find(id) != factories_.end();
}

std::vector<std::string> MenuManager::list_registered() const {
  std::vector<std::string> result;
  result.reserve(factories_.size());
  for (const auto& [id, _] : factories_) {
    result.push_back(id);
  }
  return result;
}

bool MenuManager::load(const std::string& menu_id) {
  // Check if menu exists
  auto it = factories_.find(menu_id);
  if (it == factories_.end()) {
    log_error("Cannot load menu '" + menu_id + "': not registered");
    return false;
  }

  // Save state of current menu if it wants to preserve
  if (!stack_.empty()) {
    auto& current_entry = stack_.back();
    if (current_entry.menu && current_entry.menu->preserve_state()) {
      current_entry.saved_state = current_entry.menu->save_state();
      log_info("Saved state for menu: " + current_entry.menu->id());
    }

    // Call on_unload for current menu
    if (current_entry.menu) {
      current_entry.menu->on_unload();
    }
  }

  // Create new menu instance
  std::unique_ptr<Menu> new_menu = it->second();
  if (!new_menu) {
    log_error("Failed to create menu instance: " + menu_id);
    return false;
  }

  log_info("Loading menu: " + menu_id);

  // Build the menu UI (requires TUIContext)
  if (!ctx_) {
    log_error("Cannot build menu: TUIContext not set");
    return false;
  }
  new_menu->build(*ctx_);
  log_info("Built menu: " + menu_id);

  // Call on_load
  new_menu->on_load();

  // Push onto stack
  MenuStackEntry entry;
  entry.menu = std::move(new_menu);
  entry.menu_id = menu_id;
  stack_.push_back(std::move(entry));

  // Clear forward stack (can't go forward after loading new menu)
  forward_stack_.clear();

  update_mode_for_active_menu();

  log_info("Menu loaded: " + menu_id + " (stack depth: " +
           std::to_string(stack_.size()) + ")");
  return true;
}

bool MenuManager::back() {
  if (stack_.size() <= 1) {
    log_warn("Cannot go back: at bottom of stack");
    return false;
  }

  // Save current menu ID for forward stack
  MenuStackEntry& current_entry = stack_.back();
  std::string current_id = current_entry.menu_id;

  // Save state if menu wants to preserve
  if (current_entry.menu && current_entry.menu->preserve_state()) {
    current_entry.saved_state = current_entry.menu->save_state();
    log_info("Saved state for menu: " + current_entry.menu->id());
  }

  // Call on_unload
  if (current_entry.menu) {
    current_entry.menu->on_unload();
  }

  // Pop from stack
  stack_.pop_back();

  // Add to forward stack
  forward_stack_.push_back(current_id);

  // Restore previous menu
  MenuStackEntry& prev_entry = stack_.back();
  if (prev_entry.menu) {
    // Restore state if available
    if (prev_entry.menu->preserve_state() &&
        !prev_entry.saved_state.data.empty()) {
      prev_entry.menu->restore_state(prev_entry.saved_state);
      log_info("Restored state for menu: " + prev_entry.menu->id());
    }

    // Call on_resume
    prev_entry.menu->on_resume();

    log_info("Navigated back to menu: " + prev_entry.menu->id() +
             " (stack depth: " + std::to_string(stack_.size()) + ")");
  }

  update_mode_for_active_menu();

  return true;
}

bool MenuManager::forward() {
  if (forward_stack_.empty()) {
    log_warn("Cannot go forward: no forward history");
    return false;
  }

  // Get menu ID from forward stack
  std::string menu_id = forward_stack_.back();
  forward_stack_.pop_back();

  // Load the menu (this will clear forward_stack, but we already popped)
  // We need to temporarily save forward stack
  auto saved_forward_stack = forward_stack_;
  bool result = load(menu_id);

  // Restore forward stack (minus the one we just loaded)
  forward_stack_ = saved_forward_stack;

  if (result) {
    log_info("Navigated forward to menu: " + menu_id);
    update_mode_for_active_menu();
  }

  return result;
}

Menu* MenuManager::current() const {
  if (stack_.empty()) {
    return nullptr;
  }
  return stack_.back().menu.get();
}

size_t MenuManager::stack_depth() const {
  return stack_.size();
}

void MenuManager::clear_stack() {
  log_info("Clearing menu stack");

  // Call on_unload for all menus
  for (auto& entry : stack_) {
    if (entry.menu) {
      entry.menu->on_unload();
    }
  }

  stack_.clear();
  forward_stack_.clear();

  update_mode_for_active_menu();

  log_info("Menu stack cleared");
}

void MenuManager::log_info(const std::string& msg) const {
  if (log_) {
    log_->info("MenuManager", msg);
  }
}

void MenuManager::log_warn(const std::string& msg) const {
  if (log_) {
    log_->warn("MenuManager", msg);
  }
}

void MenuManager::log_error(const std::string& msg) const {
  if (log_) {
    log_->error("MenuManager", msg);
  }
}

void MenuManager::update_mode_for_active_menu() {
  if (!ctx_) {
    return;
  }

  if (current()) {
    ctx_->modes().enter(Mode::NORMAL);
  } else {
    ctx_->modes().enter(Mode::INSERT);
  }
}

} // namespace nazg::tui

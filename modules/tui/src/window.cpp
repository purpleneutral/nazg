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

#include "tui/window.hpp"
#include "tui/managers/pane_reaper.hpp"
#include "blackbox/logger.hpp"
#include <ftxui/dom/elements.hpp>
#include <algorithm>

namespace nazg::tui {

using namespace ftxui;

Window::Window(const std::string& shell, nazg::blackbox::logger* log)
    : default_shell_(shell), log_(log) {
  // Create initial pane
  active_pane_ = create_pane(shell);
  if (active_pane_) {
    layout_.set_root_pane(active_pane_);
    log_debug("Window initialized with initial pane");
  }
}

Pane* Window::create_pane(const std::string& shell) {
  auto pane = std::make_shared<Pane>(shell);
  Pane* ptr = pane.get();
  panes_.push_back(std::move(pane));
  log_debug("Created pane (pid=" + std::to_string(ptr ? ptr->pid() : -1) + ")");
  return ptr;
}

Pane* Window::split_active(SplitDirection direction, const std::string& shell) {
  Pane* result = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!active_pane_) {
      return nullptr;
    }

    if (zoomed_) {
      zoomed_ = false;
      zoom_target_ = nullptr;
    }

    std::string shell_cmd = shell.empty() ? default_shell_ : shell;
    Pane* new_pane = create_pane(shell_cmd);

    if (!layout_.split_pane(active_pane_, new_pane, direction)) {
      // Failed to split, remove the pane we just created
      panes_.erase(std::remove_if(panes_.begin(), panes_.end(),
                                  [new_pane](const auto& p) { return p.get() == new_pane; }),
                   panes_.end());
      return nullptr;
    }

    // Update active state for new pane
    set_active_locked(new_pane);
    result = new_pane;
  }

  // Log outside mutex
  if (result) {
    log_debug("split_active succeeded; pane count=" + std::to_string(panes_.size()));
  }

  return result;
}

bool Window::close_active() {
  log_debug("close_active() entered");

  // Extract the pane shared_ptr BEFORE destroying it, so we can release mutex first
  int remaining_count = 0;
  bool success = false;
  int queued_pid = -1;
  std::shared_ptr<Pane> pane_for_cleanup;

  {
    log_debug("close_active() acquiring mutex");
    std::lock_guard<std::mutex> lock(mutex_);
    log_debug("close_active() mutex acquired");

    if (!active_pane_ || panes_.size() <= 1) {
      return false; // Can't close the last pane
    }

    if (zoomed_ && active_pane_ == zoom_target_) {
      zoomed_ = false;
      zoom_target_ = nullptr;
    }

    // Store the pane pointer before removing from layout
    Pane* pane_to_remove = active_pane_;

    log_debug("close_active() removing pane from layout");
    // Remove from layout
    if (!layout_.remove_pane(active_pane_)) {
      log_debug("close_active() failed: remove_pane returned false");
      return false;
    }
    log_debug("close_active() remove_pane returned success");
    log_debug("close_active() removed pane from layout");

    // Find a new active pane (first remaining pane) - get list ONCE
    auto remaining = layout_.get_all_panes();
    log_debug("close_active() collected remaining panes (count=" + std::to_string(remaining.size()) + ")");
    Pane* new_active = remaining.empty() ? nullptr : remaining[0];
    remaining_count = remaining.size();

    // Move the pane into the async cleanup queue (cleanup will run in update loop)
    auto it = std::find_if(panes_.begin(), panes_.end(),
                           [pane_to_remove](const auto& p) {
                             return p.get() == pane_to_remove;
                           });
    if (it != panes_.end()) {
      pane_for_cleanup = *it;
      queued_pid = pane_to_remove ? pane_to_remove->pid() : -1;
      panes_.erase(it);
      log_debug("close_active() queued pane for async cleanup");
    }

    set_active_locked(new_active);
    log_debug("close_active() set new active pane");
    success = true;
    log_debug("close_active() releasing mutex");
  }

  // Mutex is released here. Now safe to log and destroy the pane outside the mutex.
  if (success) {
    log_debug("[close_active] Closed pane, " + std::to_string(remaining_count) + " remaining");
    if (pane_for_cleanup) {
      log_debug("[close_active] Queued pane for async cleanup (pid=" + std::to_string(queued_pid) + ")");
    }
  }

  if (pane_for_cleanup) {
    PaneReaper::instance().enqueue(std::move(pane_for_cleanup), log_);
    log_debug("close_active() enqueued pane to reaper");
  }

  return success;
}

bool Window::navigate(char direction) {
  bool success = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!active_pane_) {
      return false;
    }

    Pane* next = layout_.get_pane_in_direction(active_pane_, direction);
    if (!next) {
      return false;
    }

    set_active_locked(next);
    success = true;
  }

  // Log outside mutex
  if (success) {
    log_debug(std::string("navigate moved to direction '") + direction + "'");
  }

  return success;
}

void Window::set_active(Pane* pane) {
  std::lock_guard<std::mutex> lock(mutex_);
  set_active_locked(pane);
}

void Window::set_active_locked(Pane* pane) {
  if (active_pane_) {
    active_pane_->set_active(false);
  }
  active_pane_ = pane;
  if (active_pane_) {
    active_pane_->set_active(true);
    if (zoomed_) {
      zoom_target_ = active_pane_;
    }
    // Removed log_info call to avoid mutex deadlock with logger
  } else {
    // Removed log_warn call to avoid mutex deadlock with logger
  }
}

void Window::send_input(const std::string& data) {
  std::shared_ptr<Pane> pane_ptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pane : panes_) {
      if (pane.get() == active_pane_) {
        pane_ptr = pane;
        break;
      }
    }
  }

  if (pane_ptr) {
    pane_ptr->send_input(data);
  } else {
    log_warn("send_input skipped (no active pane)");
  }
}

void Window::update() {
  std::vector<std::shared_ptr<Pane>> panes_snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    panes_snapshot = panes_;
  }

  for (const auto& pane : panes_snapshot) {
    if (pane) {
      pane->update();
    }
  }
}

bool Window::save_layout() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!layout_.root() || panes_.empty()) {
    log_warn("save_layout skipped (no panes)");
    return false;
  }

  auto snapshot = layout_.snapshot();
  if (snapshot.leaf_count == 0) {
    log_warn("save_layout snapshot empty");
    return false;
  }

  auto leaves = layout_.get_all_panes();
  size_t active_index = 0;
  if (active_pane_) {
    auto it = std::find(leaves.begin(), leaves.end(), active_pane_);
    if (it != leaves.end()) {
      active_index = static_cast<size_t>(std::distance(leaves.begin(), it));
    }
  }

  saved_layout_ = SavedLayout{std::move(snapshot), active_index};
  log_info("Layout saved (" + std::to_string(leaves.size()) + " panes)");
  return true;
}

bool Window::restore_layout() {
  std::vector<std::shared_ptr<Pane>> old_panes;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!saved_layout_) {
      log_warn("restore_layout skipped (no saved layout)");
      return false;
    }

    const auto& saved = *saved_layout_;
    if (saved.snapshot.leaf_count == 0) {
      log_warn("restore_layout skipped (saved layout empty)");
      return false;
    }

    std::vector<std::shared_ptr<Pane>> new_panes;
    new_panes.reserve(saved.snapshot.leaf_count);
    for (size_t i = 0; i < saved.snapshot.leaf_count; ++i) {
      new_panes.push_back(std::make_shared<Pane>(default_shell_));
    }

    std::vector<Pane*> leaf_ptrs;
    leaf_ptrs.reserve(new_panes.size());
    for (auto& pane : new_panes) {
      leaf_ptrs.push_back(pane.get());
    }

    Layout new_layout;
    if (!new_layout.restore_from_snapshot(saved.snapshot, leaf_ptrs)) {
      log_error("restore_layout failed (snapshot restore error)");
      return false;
    }

    size_t active_index = saved.active_leaf_index;
    if (leaf_ptrs.empty()) {
      log_error("restore_layout failed (no panes produced)");
      return false;
    }
    if (active_index >= leaf_ptrs.size()) {
      active_index = 0;
    }

    old_panes = std::move(panes_);
    panes_ = std::move(new_panes);
    layout_ = std::move(new_layout);
    zoomed_ = false;
    zoom_target_ = nullptr;

    set_active_locked(leaf_ptrs[active_index]);
    log_info("Layout restored (" + std::to_string(panes_.size()) + " panes)");
  }

  for (auto& pane : old_panes) {
    if (pane) {
      PaneReaper::instance().enqueue(std::move(pane), log_);
    }
  }

  return true;
}

Element Window::render_node(const Layout::Node* node,
                            int width,
                            int height,
                            const Theme& theme) {
  if (!node) {
    return filler();
  }

  const int clamped_width = std::max(1, width);
  const int clamped_height = std::max(1, height);

  if (node->is_leaf() || !node->left || !node->right) {
    Pane* pane = node->pane;
    if (!pane) {
      return filler();
    }

    pane->resize(clamped_width, clamped_height);
    return pane->render(pane == active_pane_,
                        theme.active_border,
                        theme.inactive_border) |
           flex_grow;
  }

  const float ratio = std::clamp(node->split_ratio, 0.05f, 0.95f);

  if (node->split_dir == SplitDirection::HORIZONTAL) {
    const int separator_width = clamped_width > 1 ? 1 : 0;
    const int usable_width = std::max(1, clamped_width - separator_width);

    int left_width = std::max(1, static_cast<int>(usable_width * ratio));
    int right_width = std::max(1, usable_width - left_width);

    // Adjust for rounding
    const int allocated = left_width + right_width;
    if (allocated != usable_width) {
      right_width += (usable_width - allocated);
      if (right_width < 1) {
        right_width = 1;
        left_width = std::max(1, usable_width - right_width);
      }
    }

    auto left_element = render_node(node->left.get(), left_width, clamped_height, theme) |
                        size(WIDTH, EQUAL, left_width);
    auto right_element = render_node(node->right.get(), right_width, clamped_height, theme) |
                         size(WIDTH, EQUAL, right_width);

    if (separator_width == 0) {
      return hbox({
        left_element | flex_grow,
        right_element | flex_grow,
      });
    }

    return hbox({
      left_element,
      separator(),
      right_element,
    });
  }

  const int separator_height = clamped_height > 1 ? 1 : 0;
  const int usable_height = std::max(1, clamped_height - separator_height);

  int top_height = std::max(1, static_cast<int>(usable_height * ratio));
  int bottom_height = std::max(1, usable_height - top_height);

  // Adjust for rounding
  const int allocated_height = top_height + bottom_height;
  if (allocated_height != usable_height) {
    bottom_height += (usable_height - allocated_height);
    if (bottom_height < 1) {
      bottom_height = 1;
      top_height = std::max(1, usable_height - bottom_height);
    }
  }

  auto top_element = render_node(node->left.get(), clamped_width, top_height, theme) |
                     size(HEIGHT, EQUAL, top_height);
  auto bottom_element = render_node(node->right.get(), clamped_width, bottom_height, theme) |
                        size(HEIGHT, EQUAL, bottom_height);

  if (separator_height == 0) {
    return vbox({
      top_element | flex_grow,
      bottom_element | flex_grow,
    });
  }

  return vbox({
    top_element,
    separator(),
    bottom_element,
  });
}

Element Window::render(int width, int height, const Theme& theme) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (panes_.empty() || !layout_.root()) {
    return text("No panes") | center;
  }

  const int clamped_width = std::max(1, width);
  const int clamped_height = std::max(1, height);

  if (zoomed_ && zoom_target_) {
    zoom_target_->resize(clamped_width, clamped_height);
    return zoom_target_->render(true,
                                theme.active_border,
                                theme.inactive_border) |
           size(WIDTH, EQUAL, clamped_width) |
           size(HEIGHT, EQUAL, clamped_height);
  }

  Element content = render_node(layout_.root(), clamped_width, clamped_height, theme);
  return content |
         size(WIDTH, GREATER_THAN, clamped_width) |
         size(HEIGHT, GREATER_THAN, clamped_height);
}

bool Window::has_alive_panes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::any_of(panes_.begin(), panes_.end(),
                     [](const auto& p) { return p->is_alive(); });
}

void Window::remove_pane(Pane* pane) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (pane == zoom_target_) {
    zoomed_ = false;
    zoom_target_ = nullptr;
  }
  panes_.erase(std::remove_if(panes_.begin(), panes_.end(),
                              [pane](const auto& p) { return p.get() == pane; }),
               panes_.end());
}

bool Window::toggle_zoom() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!active_pane_) {
    log_warn("toggle_zoom with no active pane");
    return false;
  }

  if (zoomed_) {
    zoomed_ = false;
    zoom_target_ = nullptr;
    log_debug("toggle_zoom -> off");
    return true;
  }

  zoomed_ = true;
  zoom_target_ = active_pane_;
  log_debug("toggle_zoom -> on");
  return true;
}

bool Window::active_process_info(pid_t& pid_out, bool& alive_out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_pane_) {
    return false;
  }
  pid_out = active_pane_->pid();
  alive_out = active_pane_->is_alive();
  return true;
}

void Window::set_name(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  name_ = name;
  log_debug("Window renamed to " + name);
}

std::string Window::name() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return name_;
}

void Window::log_debug(const std::string& msg) const {
  if (log_) {
    log_->debug("TUI", std::string("[Window] ") + msg);
  }
}

void Window::log_info(const std::string& msg) const {
  if (log_) {
    log_->info("TUI", std::string("[Window] ") + msg);
  }
}

void Window::log_warn(const std::string& msg) const {
  if (log_) {
    log_->warn("TUI", std::string("[Window] ") + msg);
  }
}

void Window::log_error(const std::string& msg) const {
  if (log_) {
    log_->error("TUI", std::string("[Window] ") + msg);
  }
}

} // namespace nazg::tui

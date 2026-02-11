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

#include "tui/statusbar.hpp"
#include <ftxui/dom/elements.hpp>
#include <algorithm>

namespace nazg::tui {

using namespace ftxui;

void StatusBar::set_process_info(pid_t pid, bool alive) {
  pid_ = pid;
  process_alive_ = alive;
}

std::string StatusBar::build_left() const {
  std::string left = " ";

  // Prefix indicator
  if (prefix_active_) {
    left += "[PREFIX] ";
  }

  // Mode
  left += "▶ nazg ";
  if (!mode_.empty() && mode_ != "INSERT") {
    left += "(" + mode_ + ") ";
  }

  if (!window_summary_.empty()) {
    left += window_summary_ + " ";
  }

  return left;
}

std::string StatusBar::build_info() const {
  if (!info_.empty()) {
    return " | " + info_;
  }
  return "";
}

std::string StatusBar::build_right() const {
  std::string right;

  if (process_alive_) {
    std::string hint = prefix_hint_.empty() ? "Prefix" : prefix_hint_ + ":Prefix";
    right = " " + hint + " | F7:Quit";
    if (prefix_active_) {
      right = " ?:Help | F7:Quit";
    }
    if (pid_ > 0 && !prefix_active_) {
      right += " | PID:" + std::to_string(pid_);
    }
  } else {
    right = " Process ended | F7:Quit";
  }

  right += " ";
  return right;
}

Element StatusBar::render(int width, const Theme& theme) const {
  std::string left = build_left();
  std::string info = build_info();
  std::string right = build_right();

  // Calculate padding
  int left_width = left.size() + info.size();
  int right_width = right.size();
  int padding = std::max(0, width - left_width - right_width);

  std::string status_full = left + info + std::string(padding, ' ') + right;

  // Apply styling based on prefix state
  auto status_element = text(status_full);

  if (prefix_active_) {
    // Highlighted when prefix is active
    return status_element
         | bgcolor(theme.status_accent)
         | color(Color::Black)
         | bold;
  } else {
    return status_element
         | bgcolor(theme.status_bg)
         | color(theme.status_fg)
         | bold;
  }
}

void StatusBar::set_window_summary(int index, int total, bool zoomed, const std::string& title) {
  if (total <= 0 || index < 0 || index >= total) {
    window_summary_.clear();
    return;
  }

  std::string summary = "[" + std::to_string(index + 1) + "/" + std::to_string(total) + "]";
  if (!title.empty()) {
    summary += " " + title;
  }
  if (zoomed) {
    summary += " (zoom)";
  }
  window_summary_ = summary;
}

} // namespace nazg::tui

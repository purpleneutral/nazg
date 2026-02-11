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

#include "tui/managers/mode_manager.hpp"

namespace nazg::tui {

ModeManager::ModeManager() = default;

bool ModeManager::enter(Mode mode) {
  if (current_mode_ == mode) {
    return false;
  }

  Mode old_mode = current_mode_;
  previous_mode_ = current_mode_;
  current_mode_ = mode;

  // Deactivate prefix when entering non-prefix mode
  if (mode != Mode::PREFIX) {
    prefix_active_ = false;
  }

  if (mode_change_callback_) {
    mode_change_callback_(old_mode, mode);
  }

  return true;
}

bool ModeManager::exit_to_previous() {
  if (current_mode_ == previous_mode_) {
    return false;
  }
  return enter(previous_mode_);
}

bool ModeManager::is_prefix_active() const {
  return prefix_active_;
}

void ModeManager::activate_prefix(int timeout_ms) {
  prefix_active_ = true;
  prefix_timeout_ms_ = timeout_ms;
  prefix_time_ = std::chrono::steady_clock::now();
  enter(Mode::PREFIX);
}

void ModeManager::deactivate_prefix() {
  prefix_active_ = false;
  if (current_mode_ == Mode::PREFIX) {
    exit_to_previous();
  }
}

bool ModeManager::check_prefix_timeout() {
  if (!prefix_active_) {
    return false;
  }

  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - prefix_time_);

  if (elapsed.count() >= prefix_timeout_ms_) {
    deactivate_prefix();
    return true;
  }

  return false;
}

std::string ModeManager::mode_name(Mode mode) {
  switch (mode) {
    case Mode::NORMAL: return "NORMAL";
    case Mode::INSERT: return "INSERT";
    case Mode::VISUAL: return "VISUAL";
    case Mode::COMMAND: return "COMMAND";
    case Mode::PREFIX: return "PREFIX";
  }
  return "UNKNOWN";
}

void ModeManager::on_mode_change(std::function<void(Mode, Mode)> callback) {
  mode_change_callback_ = std::move(callback);
}

} // namespace nazg::tui

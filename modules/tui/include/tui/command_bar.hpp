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

#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <string>
#include <vector>

namespace nazg::tui {

class TUIContext;

/**
 * @brief Minimal command bar inspired by ctrlCore's command palette.
 *
 * When active, it captures keyboard input, allows editing the buffer,
 * and executes commands via the TUIContext CommandManager on Enter.
 */
class CommandBar {
public:
  CommandBar() = default;

  /**
   * @brief Activate command bar and clear buffer.
   */
  void activate();

  /**
   * @brief Deactivate command bar without executing buffer.
   */
  void deactivate();

  /**
   * @brief Whether the command bar is currently capturing input.
   */
  bool active() const { return active_; }

  /**
   * @brief Handle keyboard event. Returns true if consumed.
   */
  bool handle_event(const ftxui::Event& event, TUIContext& ctx);

  /**
   * @brief Render the command bar element.
   */
  ftxui::Element render(int width) const;

private:
  bool execute_buffer(TUIContext& ctx);

  bool active_ = false;
  std::string buffer_;
};

} // namespace nazg::tui

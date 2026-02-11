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

#include "tui/component_base.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>

namespace nazg::tui {

/**
 * @brief Wrapper that bridges FTXUI components with ComponentBase
 *
 * This allows FTXUI Renderer components to be used within the Menu system.
 */
class FTXUIComponent : public ComponentBase {
public:
  /**
   * @brief Create from a render function
   * @param id Component ID
   * @param render_fn Function that generates FTXUI elements
   */
  FTXUIComponent(const std::string& id,
                 std::function<ftxui::Element()> render_fn)
      : id_(id), render_fn_(render_fn) {}

  /**
   * @brief Create an interactive component
   * @param id Component ID
   * @param render_fn Function that generates FTXUI elements
   * @param event_fn Function that handles events (returns true if handled)
   */
  FTXUIComponent(const std::string& id,
                 std::function<ftxui::Element()> render_fn,
                 std::function<bool(const ftxui::Event&)> event_fn)
      : id_(id), render_fn_(render_fn), event_fn_(event_fn), focusable_(true) {}

  // ComponentBase interface
  std::string id() const override { return id_; }
  ComponentType type() const override { return ComponentType::LEAF; }

  ftxui::Element render(int width, int height, const Theme& theme) override {
    (void)width;   // Render function handles sizing
    (void)height;
    (void)theme;  // Can be used by render_fn if needed

    if (render_fn_) {
      return render_fn_();
    }
    return ftxui::text("Empty component");
  }

  bool handle_event(const ftxui::Event& event) override {
    if (event_fn_) {
      return event_fn_(event);
    }
    return false;
  }

  bool is_focusable() const override { return focusable_; }

private:
  std::string id_;
  std::function<ftxui::Element()> render_fn_;
  std::function<bool(const ftxui::Event&)> event_fn_;
  bool focusable_ = false;
};

} // namespace nazg::tui

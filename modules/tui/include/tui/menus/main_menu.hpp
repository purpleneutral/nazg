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

#include "tui/menu.hpp"

namespace nazg {
namespace blackbox {
class logger;
}

namespace nexus {
class Store;
}

namespace tui {

class MainMenu : public Menu {
public:
  MainMenu(nazg::blackbox::logger* log = nullptr);

  std::string id() const override { return "main"; }
  std::string title() const override { return "Main Menu"; }

  void build(TUIContext& ctx) override;

private:
  nazg::blackbox::logger* log_;
  TUIContext* ctx_ = nullptr;
  int selected_index_ = 0;

  std::vector<std::string> menu_items_ = {
    "Docker Management",
    "System Tools",
    "Git Management",
    "Settings",
    "Exit"
  };

  void on_item_selected(int index);
};

} // namespace tui
} // namespace nazg

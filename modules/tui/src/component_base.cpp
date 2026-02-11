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

#include "tui/component_base.hpp"
#include <algorithm>

namespace nazg::tui {

void ComponentBase::add_child(std::unique_ptr<ComponentBase> child) {
  if (!child) {
    return;
  }

  child->set_parent(this);
  child->on_mount();
  children_.push_back(std::move(child));
}

bool ComponentBase::remove_child(const std::string& child_id) {
  auto it = std::find_if(children_.begin(), children_.end(),
                         [&child_id](const auto& child) {
                           return child->id() == child_id;
                         });

  if (it == children_.end()) {
    return false;
  }

  (*it)->on_unmount();
  (*it)->set_parent(nullptr);
  children_.erase(it);
  return true;
}

} // namespace nazg::tui

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

#include "tui/managers/pane_reaper.hpp"
#include "tui/pane.hpp"
#include "blackbox/logger.hpp"

namespace nazg::tui {

PaneReaper& PaneReaper::instance() {
  static PaneReaper instance;
  return instance;
}

PaneReaper::PaneReaper()
    : worker_([this]() { worker_loop(); }) {
}

PaneReaper::~PaneReaper() {
  shutdown();
}

void PaneReaper::enqueue(std::shared_ptr<Pane> pane, nazg::blackbox::logger* log) {
  if (!pane) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) {
      return;
    }
    queue_.push(Item{std::move(pane), log});
  }
  cv_.notify_one();
}

void PaneReaper::shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_) {
      return;
    }
    stop_ = true;
  }
  cv_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void PaneReaper::worker_loop() {
  for (;;) {
    Item item;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
      if (stop_ && queue_.empty()) {
        break;
      }
      item = std::move(queue_.front());
      queue_.pop();
    }

    const pid_t pid = item.pane ? item.pane->pid() : -1;
    item.pane.reset();
    if (item.log) {
      item.log->debug("TUI",
                      std::string("[PaneReaper] Pane destroyed (pid=") +
                          std::to_string(pid) + ")");
    }
  }
}

} // namespace nazg::tui

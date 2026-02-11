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

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace nazg::blackbox {
class logger;
}

namespace nazg::tui {

class Pane;

class PaneReaper {
public:
  static PaneReaper& instance();

  void enqueue(std::shared_ptr<Pane> pane, nazg::blackbox::logger* log);
  void shutdown();

private:
  PaneReaper();
  ~PaneReaper();

  PaneReaper(const PaneReaper&) = delete;
  PaneReaper& operator=(const PaneReaper&) = delete;

  void worker_loop();

  struct Item {
    std::shared_ptr<Pane> pane;
    nazg::blackbox::logger* log = nullptr;
  };

  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<Item> queue_;
  bool stop_ = false;
  std::thread worker_;
};

} // namespace nazg::tui


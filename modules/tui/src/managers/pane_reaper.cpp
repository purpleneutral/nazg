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

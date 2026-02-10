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


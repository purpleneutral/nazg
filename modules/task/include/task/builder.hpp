#pragma once
#include "task/executor.hpp"
#include <string>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::brain {
struct Plan;
}

namespace nazg::task {

// Build orchestration
class Builder {
public:
  explicit Builder(nazg::nexus::Store *store, nazg::blackbox::logger *log = nullptr);

  // Execute a build plan
  ExecutionResult build(int64_t project_id, const nazg::brain::Plan &plan);

  // Record build result to database
  void record_build(int64_t project_id, const nazg::brain::Plan &plan,
                    const ExecutionResult &result);

private:
  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;
  Executor executor_;
};

} // namespace nazg::task

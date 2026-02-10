#pragma once
#include "brain/types.hpp"
#include <string>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::brain {

struct ProjectInfo;
struct SnapshotResult;

// Decide what action to take
class Planner {
public:
  explicit Planner(nazg::nexus::Store *store, nazg::blackbox::logger *log = nullptr);

  // Decide action based on project info and snapshot
  Plan decide(int64_t project_id, const ProjectInfo &info, const SnapshotResult &snapshot);

  // Generate test plan for detected test framework
  Plan generate_test_plan(const ProjectInfo &info);

private:
  // Generate build command for detected build system
  Plan generate_build_plan(const ProjectInfo &info);

  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;
};

} // namespace nazg::brain

#pragma once

#include "test/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::workspace {
class Manager;
}

namespace nazg {
namespace blackbox {
class logger;
}
namespace brain {
struct Plan;
}
}  // namespace nazg

namespace nazg::test {

class Adapter;

// Test execution engine
class Runner {
 public:
  Runner(nazg::nexus::Store *store, nazg::blackbox::logger *log);
  ~Runner();

  struct WorkspaceTestContext {
    ~WorkspaceTestContext();
    WorkspaceTestContext() = default;
    WorkspaceTestContext(const WorkspaceTestContext &) = delete;
    WorkspaceTestContext &operator=(const WorkspaceTestContext &) = delete;
    WorkspaceTestContext(WorkspaceTestContext &&) noexcept = default;
    WorkspaceTestContext &operator=(WorkspaceTestContext &&) noexcept = default;

    std::string temp_project_dir;
    int64_t project_id = 0;
    nazg::nexus::Store *store = nullptr;
    nazg::workspace::Manager *ws_mgr = nullptr;
    std::string db_path;

    void create_file(const std::string &relative_path,
                     const std::string &content);
    void modify_file(const std::string &relative_path,
                     const std::string &new_content = "");
    void delete_file(const std::string &relative_path);
    int64_t create_snapshot(const std::string &label = "");
    void verify_snapshot_exists(int64_t snapshot_id);
    void verify_file_content(const std::string &relative_path,
                             const std::string &expected);

  private:
    std::unique_ptr<nazg::nexus::Store> store_owner;
    std::unique_ptr<nazg::workspace::Manager> manager_owner;

    friend class Runner;
  };

  // Execute tests and return results
  TestRun execute(int64_t project_id, const nazg::brain::Plan &plan,
                  const RunOptions &opts = {});

  // Parse existing test output (for post-processing)
  TestRun parse_output(Framework framework, const std::string &output,
                       const std::string &stderr_output, int exit_code,
                       int64_t duration_ms);

  WorkspaceTestContext setup_workspace_test(const std::string &test_name);
  void teardown_workspace_test(WorkspaceTestContext &ctx);

 private:
  std::unique_ptr<Adapter> get_adapter(Framework framework);

  nazg::nexus::Store *store_;
  nazg::blackbox::logger *log_;
  std::string temp_test_root_;
};

}  // namespace nazg::test

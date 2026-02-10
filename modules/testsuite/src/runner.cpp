#include "test/runner.hpp"
#include "test/adapters.hpp"
#include "brain/types.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"
#include "workspace/manager.hpp"
#include "system/process.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace nazg::test {

Runner::Runner(nazg::nexus::Store *store, nazg::blackbox::logger *log)
    : store_(store), log_(log) {}

Runner::~Runner() = default;

Runner::WorkspaceTestContext::~WorkspaceTestContext() = default;

TestRun Runner::execute(int64_t project_id, const nazg::brain::Plan &plan,
                        const RunOptions &opts) {
  TestRun run;
  run.project_id = project_id;
  run.framework = string_to_framework(plan.test_framework);

  if (log_) {
    log_->info("TestRunner", "Executing tests using " + plan.test_framework);
  }

  // Get the adapter for this framework
  auto adapter = get_adapter(run.framework);
  if (!adapter) {
    if (log_) {
      log_->error("TestRunner", "No adapter available for framework: " + plan.test_framework);
    }
    run.exit_code = -1;
    run.errors = 1;
    run.total = 0;
    return run;
  }

  // Build command
  std::vector<std::string> cmd_parts = adapter->build_command(plan.working_dir, opts);
  if (cmd_parts.empty()) {
    if (log_) {
      log_->error("TestRunner", "Failed to build command for " + plan.test_framework);
    }
    run.exit_code = -1;
    run.errors = 1;
    run.total = 0;
    return run;
  }

  // Execute command
  auto start_time = std::chrono::steady_clock::now();

  // Build command string with proper quoting
  std::ostringstream cmd_builder;

  // Change to working directory if needed
  if (!plan.working_dir.empty()) {
    cmd_builder << "cd " << nazg::system::shell_quote(plan.working_dir) << " && ";
  }

  cmd_builder << cmd_parts[0];
  for (size_t i = 1; i < cmd_parts.size(); ++i) {
    cmd_builder << " " << nazg::system::shell_quote(cmd_parts[i]);
  }

  std::string full_command = cmd_builder.str();

  if (log_) {
    log_->info("TestRunner", "Running: " + full_command);
  }

  // Execute via system module
  nazg::system::CommandResult result = nazg::system::run_command_capture(full_command);

  auto end_time = std::chrono::steady_clock::now();
  int64_t duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

  if (log_) {
    log_->info("TestRunner", "Test execution completed in " + std::to_string(duration_ms) + "ms");
  }

  // Parse output (using combined output for both stdout and stderr)
  run = adapter->parse_output(result.output, "", result.exit_code, duration_ms);
  run.project_id = project_id;
  run.framework = string_to_framework(plan.test_framework);
  run.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  run.triggered_by = "manual";

  // Parse coverage if requested
  if (opts.collect_coverage) {
    run.coverage = adapter->parse_coverage(plan.working_dir);
  }

  // Store results in Nexus
  if (store_) {
    int64_t run_id = store_->add_test_run(
        project_id, framework_to_string(run.framework), run.timestamp, run.duration_ms,
        run.exit_code, run.total, run.passed, run.failed, run.skipped, run.errors,
        run.triggered_by);

    if (run_id > 0) {
      run.id = run_id;

      // Store individual test results
      for (const auto &test : run.cases) {
        store_->add_test_result(run_id, test.suite, test.name,
                                status_to_string(test.status), test.duration_ms,
                                test.message, test.file, test.line);
      }

      // Store coverage if available
      if (run.coverage) {
        for (const auto &[file_path, file_cov] : run.coverage->files) {
          store_->add_test_coverage(run_id, file_path, file_cov.line_coverage, 0.0,
                                    static_cast<int>(file_cov.covered_lines.size()),
                                    static_cast<int>(file_cov.covered_lines.size() +
                                                     file_cov.uncovered_lines.size()),
                                    0, 0);
        }
      }

      if (log_) {
        log_->info("TestRunner", "Test run " + std::to_string(run_id) + " stored in Nexus");
      }
    }
  }

  return run;
}

TestRun Runner::parse_output(Framework framework, const std::string &output,
                              const std::string &stderr_output, int exit_code,
                              int64_t duration_ms) {
  auto adapter = get_adapter(framework);
  if (!adapter) {
    TestRun run;
    run.framework = framework;
    run.exit_code = exit_code;
    run.duration_ms = duration_ms;
    run.errors = 1;
    return run;
  }

  return adapter->parse_output(output, stderr_output, exit_code, duration_ms);
}

std::unique_ptr<Adapter> Runner::get_adapter(Framework framework) {
  return create_adapter(framework, log_);
}

Runner::WorkspaceTestContext
Runner::setup_workspace_test(const std::string &test_name) {
  WorkspaceTestContext ctx;

  if (temp_test_root_.empty()) {
    fs::path base = fs::temp_directory_path() / "nazg-workspace-tests";
    temp_test_root_ = base.string();
  }

  fs::create_directories(temp_test_root_);

  auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  std::mt19937_64 rng(static_cast<uint64_t>(timestamp));
  std::uniform_int_distribution<uint64_t> dist;

  fs::path project_dir;
  do {
    std::ostringstream name;
    name << test_name << "-" << timestamp << "-" << std::hex << dist(rng);
    project_dir = fs::path(temp_test_root_) / name.str();
  } while (fs::exists(project_dir));

  fs::create_directories(project_dir);

  fs::path db_root = fs::path(temp_test_root_) / "db";
  fs::create_directories(db_root);
  fs::path db_path =
      db_root / (project_dir.filename().string() + ".sqlite");

  auto store = nazg::nexus::Store::create(db_path.string(), log_);
  if (!store || !store->initialize()) {
    throw std::runtime_error("Failed to initialize test store for workspace");
  }

  ctx.store_owner = std::move(store);
  ctx.store = ctx.store_owner.get();

  ctx.project_id = ctx.store->ensure_project(project_dir.string());

  ctx.manager_owner =
      std::make_unique<nazg::workspace::Manager>(ctx.store, nullptr, log_);
  ctx.ws_mgr = ctx.manager_owner.get();
  ctx.temp_project_dir = project_dir.string();
  ctx.db_path = db_path.string();

  return ctx;
}

void Runner::teardown_workspace_test(WorkspaceTestContext &ctx) {
  if (!ctx.temp_project_dir.empty()) {
    std::error_code ec;
    fs::remove_all(ctx.temp_project_dir, ec);
  }

  ctx.manager_owner.reset();
  ctx.store_owner.reset();
  ctx.store = nullptr;
  ctx.ws_mgr = nullptr;
  ctx.project_id = 0;
  ctx.temp_project_dir.clear();
  if (!ctx.db_path.empty()) {
    std::error_code ec;
    fs::remove(ctx.db_path, ec);
    ctx.db_path.clear();
  }
}

void Runner::WorkspaceTestContext::create_file(
    const std::string &relative_path, const std::string &content) {
  if (temp_project_dir.empty()) {
    throw std::runtime_error("WorkspaceTestContext not initialized");
  }

  fs::path full_path = fs::path(temp_project_dir) / relative_path;
  fs::create_directories(full_path.parent_path());

  std::ofstream out(full_path, std::ios::trunc);
  if (!out) {
    throw std::runtime_error("Failed to create file: " + full_path.string());
  }
  out << content;
  out.close();
}

void Runner::WorkspaceTestContext::modify_file(
    const std::string &relative_path, const std::string &new_content) {
  if (temp_project_dir.empty()) {
    throw std::runtime_error("WorkspaceTestContext not initialized");
  }

  fs::path full_path = fs::path(temp_project_dir) / relative_path;
  if (!fs::exists(full_path)) {
    throw std::runtime_error("Cannot modify missing file: " + full_path.string());
  }

  std::string content;
  if (new_content.empty()) {
    std::ifstream in(full_path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    content += "\n// modified";
  } else {
    content = new_content;
  }

  std::ofstream out(full_path, std::ios::trunc);
  if (!out) {
    throw std::runtime_error("Failed to write file: " + full_path.string());
  }
  out << content;
  out.close();
}

void Runner::WorkspaceTestContext::delete_file(
    const std::string &relative_path) {
  if (temp_project_dir.empty()) {
    throw std::runtime_error("WorkspaceTestContext not initialized");
  }

  fs::path full_path = fs::path(temp_project_dir) / relative_path;
  std::error_code ec;
  fs::remove(full_path, ec);
}

int64_t Runner::WorkspaceTestContext::create_snapshot(
    const std::string &label) {
  if (!ws_mgr || !store) {
    throw std::runtime_error("WorkspaceTestContext missing manager/store");
  }

  nazg::workspace::Manager::SnapshotOptions opts;
  opts.label = label.empty() ? "workspace test snapshot" : label;
  opts.trigger_type = "test";
  return ws_mgr->create_snapshot(project_id, opts);
}

void Runner::WorkspaceTestContext::verify_snapshot_exists(
    int64_t snapshot_id) {
  if (!ws_mgr) {
    throw std::runtime_error("WorkspaceTestContext missing manager");
  }

  auto snapshot = ws_mgr->get_snapshot(snapshot_id);
  if (!snapshot) {
    throw std::runtime_error("Expected snapshot to exist: #" +
                             std::to_string(snapshot_id));
  }
}

void Runner::WorkspaceTestContext::verify_file_content(
    const std::string &relative_path, const std::string &expected) {
  fs::path full_path = fs::path(temp_project_dir) / relative_path;
  std::ifstream in(full_path);
  if (!in) {
    throw std::runtime_error("Failed to open file for verification: " +
                             full_path.string());
  }

  std::ostringstream buffer;
  buffer << in.rdbuf();
  auto content = buffer.str();
  if (content != expected) {
    throw std::runtime_error("File content mismatch for " + full_path.string());
  }
}

}  // namespace nazg::test

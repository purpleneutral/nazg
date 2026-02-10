#include "task/builder.hpp"
#include "blackbox/logger.hpp"
#include "brain/types.hpp"
#include "nexus/store.hpp"

#include <sstream>

namespace nazg::task {

Builder::Builder(nazg::nexus::Store *store, nazg::blackbox::logger *log)
    : store_(store), log_(log), executor_(log) {}

ExecutionResult Builder::build(int64_t project_id, const nazg::brain::Plan &plan) {
  (void)project_id; // Will be used for recording

  if (plan.action == nazg::brain::Action::SKIP) {
    ExecutionResult result;
    result.success = true;
    result.exit_code = 0;
    if (log_) {
      log_->info("Task", "Skipping build: " + plan.reason);
    }
    return result;
  }

  if (plan.action == nazg::brain::Action::UNKNOWN) {
    ExecutionResult result;
    result.success = false;
    result.error_message = plan.reason;
    if (log_) {
      log_->error("Task", "Cannot build: " + plan.reason);
    }
    return result;
  }

  // Execute build
  // If command is a shell invocation, extract the actual command
  if (plan.command == "/bin/sh" && plan.args.size() == 2 && plan.args[0] == "-c") {
    // Use execute_shell for proper shell command handling
    return executor_.execute_shell(plan.args[1], plan.working_dir);
  } else {
    return executor_.execute(plan.command, plan.args, plan.working_dir);
  }
}

void Builder::record_build(int64_t project_id, const nazg::brain::Plan &plan,
                           const ExecutionResult &result) {
  if (!store_) return;

  // Build command string
  std::ostringstream cmd;
  cmd << plan.command;
  for (const auto &arg : plan.args) {
    cmd << " " << arg;
  }

  // Record to command history
  std::vector<std::string> args_vec = plan.args;
  store_->record_command(project_id, plan.command, args_vec,
                        result.exit_code, result.duration_ms);

  // Add event
  std::string level = result.success ? "info" : "error";
  std::string message = result.success
                           ? "Build completed (exit: " + std::to_string(result.exit_code) + ")"
                           : "Build failed (exit: " + std::to_string(result.exit_code) + ")";

  store_->add_event(project_id, level, "builder", message, "");

  if (log_) {
    log_->debug("Task", "Build result recorded to database");
  }
}

} // namespace nazg::task

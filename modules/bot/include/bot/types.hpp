#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace nazg::bot {

// Bot execution status
enum class Status {
  RUNNING,
  SUCCESS,
  WARNING,
  CRITICAL,
  ERROR
};

// Convert status to string
inline std::string status_to_string(Status s) {
  switch (s) {
    case Status::RUNNING: return "running";
    case Status::SUCCESS: return "success";
    case Status::WARNING: return "warning";
    case Status::CRITICAL: return "critical";
    case Status::ERROR: return "error";
    default: return "unknown";
  }
}

// Parse status from string
inline Status string_to_status(const std::string& s) {
  if (s == "ok" || s == "success") return Status::SUCCESS;
  if (s == "running") return Status::RUNNING;
  if (s == "warning") return Status::WARNING;
  if (s == "critical") return Status::CRITICAL;
  if (s == "error") return Status::ERROR;
  return Status::ERROR;
}

// Bot specification (definition of a bot type)
struct BotSpec {
  std::string name;
  std::string description;
  std::vector<std::string> required_inputs;
  std::map<std::string, std::string> default_config;
};

// Host configuration
struct HostConfig {
  int64_t id = 0;                    // Database ID (0 if not yet persisted)
  std::string label;                 // User-friendly name
  std::string address;               // SSH address (user@host or host)
  std::string ssh_key;               // Path to SSH key
  int ssh_port = 22;                 // SSH port
  int agent_port = 7070;             // Remote agent port (if running)
  std::vector<std::string> services; // Services to monitor (for Doctor Bot)
  std::map<std::string, std::string> extra_config; // Bot-specific config
};

// Result from bot execution
struct RunResult {
  int64_t run_id = 0;
  Status status = Status::ERROR;
  int exit_code = -1;
  std::string stdout_output;
  std::string stderr_output;
  std::string json_report;
  int64_t duration_ms = 0;
};

// Bot report data (parsed from JSON)
struct ReportData {
  std::string host;
  int64_t timestamp = 0;
  std::map<std::string, std::string> metrics;
  Status status = Status::SUCCESS;
  std::vector<std::string> notes;
  std::string raw_json;
};

// Bot host record (from database)
struct BotHost {
  int64_t id = 0;
  std::string label;
  std::string address;
  std::string ssh_config;
  int64_t last_run_at = 0;
  std::string last_status;
  int64_t created_at = 0;
  int64_t updated_at = 0;
};

// Bot run record (from database)
struct BotRun {
  int64_t id = 0;
  std::string bot_name;
  int64_t host_id = 0;
  int64_t started_at = 0;
  int64_t finished_at = 0;
  std::string status;
  int exit_code = -1;
  int64_t duration_ms = 0;
};

// Bot report record (from database)
struct BotReport {
  int64_t id = 0;
  int64_t run_id = 0;
  std::string payload_json;
  int64_t created_at = 0;
};

} // namespace nazg::bot

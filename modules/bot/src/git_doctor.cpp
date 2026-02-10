#include "bot/git_doctor.hpp"
#include "blackbox/logger.hpp"
#include "config/config.hpp"
#include "nexus/store.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

namespace nazg::bot {

GitDoctorBot::GitDoctorBot(const HostConfig& host,
                           ::nazg::config::store* cfg,
                           ::nazg::nexus::Store* store,
                           ::nazg::blackbox::logger* log)
    : BotBase(host, cfg, store, log) {
  // Initialize SSH transport
  ssh_transport_ = std::make_unique<SSHTransport>(host, log);

  // Check for agent availability
  if (!host.extra_config.empty()) {
    auto it = host.extra_config.find("agent_available");
    if (it != host.extra_config.end() && it->second == "true") {
      agent_transport_ = std::make_unique<AgentTransport>(host, log);
      prefer_agent_ = true;
    }

    // Parse git server configuration from extra_config
    auto type_it = host.extra_config.find("git_server_type");
    if (type_it != host.extra_config.end()) {
      git_server_type_ = type_it->second;
    }

    auto repo_path_it = host.extra_config.find("repo_base_path");
    if (repo_path_it != host.extra_config.end()) {
      repo_base_path_ = repo_path_it->second;
    }

    auto config_path_it = host.extra_config.find("config_path");
    if (config_path_it != host.extra_config.end()) {
      config_path_ = config_path_it->second;
    }

    auto label_it = host.extra_config.find("git_server_label");
    if (label_it != host.extra_config.end()) {
      git_server_label_ = label_it->second;
    }
  }
}

void GitDoctorBot::initialize_transports() {
  // Reserved for future use if needed
}

// Embedded git-doctor script
static const char* GIT_DOCTOR_SCRIPT = R"(#!/usr/bin/env bash
# Git Doctor Bot - Git server infrastructure health check
# Checks cgit/gitea installation, services, and repository health

set -euo pipefail

# Error handler - output diagnostic info on failure
error_handler() {
  local line=$1
  local cmd="$BASH_COMMAND"
  echo "ERROR: Git-doctor script failed at line $line" >&2
  echo "Failed command: $cmd" >&2
  exit 1
}

trap 'error_handler ${LINENO}' ERR

# Thresholds
DISK_WARN=${DISK_WARN:-85}
DISK_CRIT=${DISK_CRIT:-95}

# Configuration values (set by calling code or use defaults)
GIT_TYPE="${GIT_TYPE:-cgit}"
REPO_PATH="${REPO_PATH:-/srv/git}"
CONFIG_PATH="${CONFIG_PATH:-/etc/cgitrc}"

HOSTNAME=$(hostname -f 2>/dev/null || hostname)
TIMESTAMP=$(date +%s)

# ===== Git Server Detection =====
GIT_SERVER_INSTALLED="false"
GIT_SERVER_VERSION=""
GIT_SERVER_TYPE="$GIT_TYPE"

if command -v cgit &> /dev/null; then
  GIT_SERVER_INSTALLED="true"
  GIT_SERVER_TYPE="cgit"
  if command -v pacman &> /dev/null; then
    GIT_SERVER_VERSION=$(pacman -Q cgit 2>/dev/null | awk '{print $2}' || echo "unknown")
  elif command -v dpkg &> /dev/null; then
    GIT_SERVER_VERSION=$(dpkg -l cgit 2>/dev/null | grep cgit | awk '{print $3}' || echo "unknown")
  else
    GIT_SERVER_VERSION="installed"
  fi
elif command -v gitea &> /dev/null; then
  GIT_SERVER_INSTALLED="true"
  GIT_SERVER_TYPE="gitea"
  GIT_SERVER_VERSION=$(gitea --version 2>/dev/null | head -1 || echo "unknown")
fi

# ===== Service Status =====
NGINX_STATUS="unknown"
FCGIWRAP_STATUS="unknown"

if command -v systemctl &> /dev/null; then
  if systemctl is-active --quiet nginx 2>/dev/null; then
    NGINX_STATUS="running"
  elif systemctl list-unit-files | grep -q nginx; then
    NGINX_STATUS="stopped"
  else
    NGINX_STATUS="not_installed"
  fi

  if systemctl is-active --quiet fcgiwrap.socket 2>/dev/null; then
    FCGIWRAP_STATUS="running"
  elif systemctl list-unit-files | grep -q fcgiwrap; then
    FCGIWRAP_STATUS="stopped"
  else
    FCGIWRAP_STATUS="not_installed"
  fi
fi

# ===== Repository Health =====
REPO_COUNT=0
TOTAL_SIZE_MB=0

if [ -d "$REPO_PATH" ]; then
  REPO_COUNT=$(find "$REPO_PATH" -maxdepth 1 -type d -name "*.git" 2>/dev/null | wc -l || echo "0")
  if [ "$REPO_COUNT" -gt 0 ]; then
    # Get size safely, handling case where du might fail
    TOTAL_SIZE_BYTES=$(du -sb "$REPO_PATH" 2>/dev/null | awk '{print $1}' || echo "0")
    if [ -n "$TOTAL_SIZE_BYTES" ] && [ "$TOTAL_SIZE_BYTES" != "0" ]; then
      TOTAL_SIZE_MB=$((TOTAL_SIZE_BYTES / 1024 / 1024))
    fi
  fi
fi

# ===== Disk Space =====
DISK_USED_PCT=0
DISK_FREE_GB="unknown"

if [ -d "$REPO_PATH" ]; then
  DISK_INFO=$(df -h "$REPO_PATH" 2>/dev/null | tail -1)
  if [ -n "$DISK_INFO" ]; then
    DISK_USED_PCT=$(echo "$DISK_INFO" | awk '{print $5}' | tr -d '%')
    DISK_FREE_GB=$(echo "$DISK_INFO" | awk '{print $4}')
  fi
fi

# ===== Config File Check =====
CONFIG_EXISTS="false"
CONFIG_READABLE="false"

if [ -f "$CONFIG_PATH" ]; then
  CONFIG_EXISTS="true"
  if [ -r "$CONFIG_PATH" ]; then
    CONFIG_READABLE="true"
  fi
fi

# ===== Web UI Check =====
WEB_UI_REACHABLE="false"

if command -v ss &> /dev/null; then
  if ss -tlnp 2>/dev/null | grep -q ":80 "; then
    WEB_UI_REACHABLE="true"
  fi
elif command -v netstat &> /dev/null; then
  if netstat -tlnp 2>/dev/null | grep -q ":80 "; then
    WEB_UI_REACHABLE="true"
  fi
fi

# ===== HTTP Clone Check =====
HTTP_CLONE_WORKS="unknown"

if command -v git &> /dev/null; then
  if [ -x "/usr/lib/git-core/git-http-backend" ] || [ -x "/usr/libexec/git-core/git-http-backend" ]; then
    HTTP_CLONE_WORKS="available"
  else
    HTTP_CLONE_WORKS="not_found"
  fi
fi

# ===== SSH Git User Check =====
GIT_USER_EXISTS="false"
GIT_USER_SHELL=""

if id git &> /dev/null 2>&1; then
  GIT_USER_EXISTS="true"
  GIT_USER_SHELL=$(getent passwd git | cut -d: -f7)
fi

# ===== fcgiwrap Socket Check =====
FCGIWRAP_SOCKET_EXISTS="false"

if [ -S "/run/fcgiwrap.sock" ]; then
  FCGIWRAP_SOCKET_EXISTS="true"
elif [ -S "/var/run/fcgiwrap.socket" ]; then
  FCGIWRAP_SOCKET_EXISTS="true"
fi

# ===== Determine Overall Status =====
STATUS="ok"
NOTES="[]"

if [ "$GIT_SERVER_INSTALLED" = "false" ]; then
  STATUS="critical"
  NOTES="[\"Git server ($GIT_SERVER_TYPE) not installed\"]"
elif [ "$NGINX_STATUS" != "running" ]; then
  STATUS="critical"
  NOTES="[\"nginx not running\"]"
elif [ "$FCGIWRAP_STATUS" != "running" ] && [ "$GIT_SERVER_TYPE" = "cgit" ]; then
  STATUS="warning"
  NOTES="[\"fcgiwrap not running - HTTP access may not work\"]"
elif [ "$DISK_USED_PCT" -ge "$DISK_CRIT" ]; then
  STATUS="critical"
  NOTES="[\"Disk space critical: ${DISK_USED_PCT}% used\"]"
elif [ "$DISK_USED_PCT" -ge "$DISK_WARN" ]; then
  STATUS="warning"
  NOTES="[\"Disk space elevated: ${DISK_USED_PCT}% used\"]"
elif [ "$CONFIG_EXISTS" = "false" ]; then
  STATUS="warning"
  NOTES="[\"Config file not found: $CONFIG_PATH\"]"
fi

# ===== Output JSON Report =====
cat <<EOF
{
  "host": "$HOSTNAME",
  "timestamp": $TIMESTAMP,
  "git_server": {
    "type": "$GIT_SERVER_TYPE",
    "installed": $GIT_SERVER_INSTALLED,
    "version": "$GIT_SERVER_VERSION",
    "config_path": "$CONFIG_PATH",
    "config_exists": $CONFIG_EXISTS,
    "config_readable": $CONFIG_READABLE
  },
  "services": {
    "nginx": "$NGINX_STATUS",
    "fcgiwrap": "$FCGIWRAP_STATUS"
  },
  "repositories": {
    "path": "$REPO_PATH",
    "count": $REPO_COUNT,
    "total_size_mb": $TOTAL_SIZE_MB
  },
  "disk": {
    "path": "$REPO_PATH",
    "used_pct": $DISK_USED_PCT,
    "free": "$DISK_FREE_GB"
  },
  "checks": {
    "web_ui_reachable": $WEB_UI_REACHABLE,
    "http_clone_available": "$HTTP_CLONE_WORKS",
    "git_user_exists": $GIT_USER_EXISTS,
    "git_user_shell": "$GIT_USER_SHELL",
    "fcgiwrap_socket_exists": $FCGIWRAP_SOCKET_EXISTS
  },
  "status": "$STATUS",
  "notes": $NOTES
}
EOF
)";

std::string GitDoctorBot::get_script_content() const {
  // Build script with configuration values embedded as environment variables
  std::ostringstream script;

  // Set configuration as environment variables at the start of the script
  script << "#!/usr/bin/env bash\n";
  script << "GIT_TYPE=\"" << git_server_type_ << "\"\n";
  script << "REPO_PATH=\"" << repo_base_path_ << "\"\n";
  script << "CONFIG_PATH=\"" << config_path_ << "\"\n";
  script << "\n";

  // Append the main script content (skip the shebang line from embedded script)
  const char* script_body = GIT_DOCTOR_SCRIPT;
  // Skip first line (shebang) of embedded script
  const char* first_newline = strchr(script_body, '\n');
  if (first_newline) {
    script << (first_newline + 1);
  } else {
    script << script_body;
  }

  return script.str();
}

RunResult GitDoctorBot::execute() {
  RunResult result;
  auto start = std::chrono::steady_clock::now();

  if (log_) {
    log_->info("bot.git-doctor", "Executing on " + host_.address);
    log_->debug("bot.git-doctor", "Configuration: type=" + git_server_type_ +
                " repo_path=" + repo_base_path_ + " config=" + config_path_);
  }

  int exit_code = 0;
  std::string output;
  std::string stderr_output;
  bool executed_via_agent = false;

  std::string script = get_script_content();

  // Try agent first if available
  if (prefer_agent_ && agent_transport_) {
    if (log_) {
      log_->debug("bot.git-doctor", "Attempting execution via remote agent");
    }
    if (agent_transport_->execute_script(script, exit_code, output, stderr_output)) {
      executed_via_agent = true;
      if (log_) {
        log_->info("bot.git-doctor", "Executed via remote agent");
      }
    } else if (log_) {
      log_->warn("bot.git-doctor", "Agent execution failed, falling back to SSH");
    }
  }

  // Fall back to SSH
  if (!executed_via_agent) {
    if (log_) {
      log_->debug("bot.git-doctor", "Executing via SSH transport");
    }
    output = ssh_transport_->execute_script(script, exit_code);

    // SSH transport merges stdout and stderr, so put it in both for visibility
    stderr_output = output;

    if (log_) {
      log_->debug("bot.git-doctor", "SSH execution completed with exit code " + std::to_string(exit_code));
    }
  }

  auto end = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  result.exit_code = exit_code;
  result.duration_ms = duration_ms;
  result.stdout_output = output;
  result.stderr_output = stderr_output;

  // Parse JSON report
  if (exit_code == 0) {
    try {
      ReportData report = parse_report(output);
      result.status = report.status;
      result.json_report = output;

      if (log_) {
        log_->info("bot.git-doctor", "Health check completed: " + status_to_string(result.status) +
                   " (host=" + report.host + ")");
      }

      if (store_) {
        std::string lookup_label = !git_server_label_.empty() ? git_server_label_ : host_.label;
        if (!lookup_label.empty()) {
          if (auto server = store_->get_git_server(lookup_label)) {
            auto to_int = [](const std::string& value) -> int64_t {
              try {
                return std::stoll(value);
              } catch (...) {
                return 0;
              }
            };

            auto parse_size = [](const std::string& value) -> int64_t {
              if (value.empty()) return 0;
              char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(value.back())));
              double base = 0.0;
              try {
                base = std::stod(value);
              } catch (...) {
                return 0;
              }
              double multiplier = 1.0;
              switch (suffix) {
                case 'T': multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0; break;
                case 'G': multiplier = 1024.0 * 1024.0 * 1024.0; break;
                case 'M': multiplier = 1024.0 * 1024.0; break;
                case 'K': multiplier = 1024.0; break;
                default: multiplier = 1.0; break;
              }
              return static_cast<int64_t>(base * multiplier);
            };

            nexus::GitServerHealthRecord rec;
            rec.server_id = server->id;
            rec.timestamp = report.timestamp ? report.timestamp : std::time(nullptr);
            rec.status = status_to_string(result.status);
            rec.web_ui_reachable = report.metrics.count("web_ui_reachable") && report.metrics.at("web_ui_reachable") == "true";
            rec.http_clone_works = report.metrics.count("http_clone_available") && report.metrics.at("http_clone_available") == "available";
            rec.ssh_push_works = report.metrics.count("git_user_exists") && report.metrics.at("git_user_exists") == "true";
            if (report.metrics.count("repo_count")) {
              rec.repo_count = static_cast<int>(to_int(report.metrics.at("repo_count")));
            }
            if (report.metrics.count("total_size_mb")) {
              rec.total_size_bytes = to_int(report.metrics.at("total_size_mb")) * 1024 * 1024;
            }
            if (report.metrics.count("disk_used_pct")) {
              rec.disk_used_pct = static_cast<int>(to_int(report.metrics.at("disk_used_pct")));
            }
            if (report.metrics.count("disk_free")) {
              rec.disk_free_bytes = parse_size(report.metrics.at("disk_free"));
            }
            if (report.metrics.count("services_json")) {
              rec.service_status_json = report.metrics.at("services_json");
            }
            if (!report.notes.empty()) {
              if (report.metrics.count("notes_json")) {
                rec.notes_json = report.metrics.at("notes_json");
              }
            }

            store_->add_git_server_health(rec);
          }
        }
      }
    } catch (const std::exception& e) {
      result.status = Status::ERROR;
      result.stderr_output = std::string("Failed to parse report: ") + e.what();

      if (log_) {
        log_->error("bot.git-doctor", "JSON parse error: " + std::string(e.what()));
        log_->debug("bot.git-doctor", "Raw output: " + output.substr(0, 500));
      }
    }
  } else {
    result.status = Status::ERROR;

    if (log_) {
      log_->error("bot.git-doctor", "Script failed with exit code " + std::to_string(exit_code));
      if (!output.empty()) {
        log_->debug("bot.git-doctor", "Output: " + output.substr(0, 500));
      }
    }
  }

  return result;
}

ReportData GitDoctorBot::parse_report(const std::string& json_output) const {
  // Simple JSON parsing for the report
  ReportData report;
  report.raw_json = json_output;

  // Extract status field
  size_t status_pos = json_output.find("\"status\": \"");
  if (status_pos != std::string::npos) {
    size_t start = status_pos + 11;
    size_t end = json_output.find("\"", start);
    if (end != std::string::npos) {
      std::string status_str = json_output.substr(start, end - start);
      report.status = string_to_status(status_str);
    }
  }

  // Extract host
  size_t host_pos = json_output.find("\"host\": \"");
  if (host_pos != std::string::npos) {
    size_t start = host_pos + 9;
    size_t end = json_output.find("\"", start);
    if (end != std::string::npos) {
      report.host = json_output.substr(start, end - start);
    }
  }

  // Extract timestamp
  size_t ts_pos = json_output.find("\"timestamp\": ");
  if (ts_pos != std::string::npos) {
    size_t start = ts_pos + 13;
    size_t end = json_output.find_first_of(",}", start);
    if (end != std::string::npos) {
      std::string ts_str = json_output.substr(start, end - start);
      report.timestamp = std::stoll(ts_str);
    }
  }

  auto extract_section = [&](const std::string& key) -> std::string {
    std::string marker = "\"" + key + "\": ";
    auto pos = json_output.find(marker);
    if (pos == std::string::npos) {
      return {};
    }
    pos = json_output.find('{', pos);
    if (pos == std::string::npos) {
      return {};
    }
    int depth = 0;
    for (size_t i = pos; i < json_output.size(); ++i) {
      if (json_output[i] == '{') depth++;
      else if (json_output[i] == '}') {
        depth--;
        if (depth == 0) {
          return json_output.substr(pos, i - pos + 1);
        }
      }
    }
    return {};
  };

  auto extract_array = [&](const std::string& key) -> std::string {
    std::string marker = "\"" + key + "\": ";
    auto pos = json_output.find(marker);
    if (pos == std::string::npos) {
      return {};
    }
    pos = json_output.find('[', pos);
    if (pos == std::string::npos) {
      return {};
    }
    int depth = 0;
    for (size_t i = pos; i < json_output.size(); ++i) {
      if (json_output[i] == '[') depth++;
      else if (json_output[i] == ']') {
        depth--;
        if (depth == 0) {
          return json_output.substr(pos, i - pos + 1);
        }
      }
    }
    return {};
  };

  auto find_bool = [](const std::string& text, const std::string& key) -> std::optional<bool> {
    std::string marker = "\"" + key + "\":";
    auto pos = text.find(marker);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    pos += marker.size();
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) pos++;
    if (text.compare(pos, 4, "true") == 0) {
      return true;
    }
    if (text.compare(pos, 5, "false") == 0) {
      return false;
    }
    return std::nullopt;
  };

  auto find_string = [](const std::string& text, const std::string& key) -> std::optional<std::string> {
    std::string marker = "\"" + key + "\": ";
    auto pos = text.find(marker);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    pos = text.find('"', pos + marker.size());
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    auto end = text.find('"', pos + 1);
    if (end == std::string::npos) {
      return std::nullopt;
    }
    return text.substr(pos + 1, end - pos - 1);
  };

  auto find_number = [](const std::string& text, const std::string& key) -> std::optional<int64_t> {
    std::string marker = "\"" + key + "\": ";
    auto pos = text.find(marker);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    pos += marker.size();
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) pos++;
    std::size_t end = pos;
    while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.')) {
      end++;
    }
    if (end == pos) {
      return std::nullopt;
    }
    try {
      return static_cast<int64_t>(std::stod(text.substr(pos, end - pos)));
    } catch (...) {
      return std::nullopt;
    }
  };

  if (auto section = extract_section("checks"); !section.empty()) {
    if (auto val = find_bool(section, "web_ui_reachable")) {
      report.metrics["web_ui_reachable"] = *val ? "true" : "false";
    }
    if (auto val = find_string(section, "http_clone_available")) {
      report.metrics["http_clone_available"] = *val;
    }
    if (auto val = find_bool(section, "git_user_exists")) {
      report.metrics["git_user_exists"] = *val ? "true" : "false";
    }
    if (auto val = find_bool(section, "fcgiwrap_socket_exists")) {
      report.metrics["fcgiwrap_socket_exists"] = *val ? "true" : "false";
    }
  }

  if (auto section = extract_section("repositories"); !section.empty()) {
    if (auto val = find_number(section, "count")) {
      report.metrics["repo_count"] = std::to_string(*val);
    }
    if (auto val = find_number(section, "total_size_mb")) {
      report.metrics["total_size_mb"] = std::to_string(*val);
    }
    if (auto val = find_string(section, "path")) {
      report.metrics["repo_path"] = *val;
    }
  }

  if (auto section = extract_section("disk"); !section.empty()) {
    if (auto val = find_number(section, "used_pct")) {
      report.metrics["disk_used_pct"] = std::to_string(*val);
    }
    if (auto val = find_string(section, "free")) {
      report.metrics["disk_free"] = *val;
    }
  }

  if (auto section = extract_section("services"); !section.empty()) {
    report.metrics["services_json"] = section;
  }

  if (auto notes_json = extract_array("notes"); !notes_json.empty()) {
    report.metrics["notes_json"] = notes_json;
    std::string content = notes_json.substr(1, notes_json.size() - 2);
    std::stringstream ss(content);
    std::string item;
    while (std::getline(ss, item, ',')) {
      auto trim = [](std::string s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        if (!s.empty() && s.front() == '"' && s.back() == '"') {
          s = s.substr(1, s.size() - 2);
        }
        return s;
      };
      item = trim(item);
      if (!item.empty()) {
        report.notes.push_back(item);
      }
    }
  }

  return report;
}

} // namespace nazg::bot

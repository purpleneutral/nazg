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

#include "bot/doctor.hpp"
#include "blackbox/logger.hpp"
#include "config/config.hpp"
#include "nexus/store.hpp"
#include <chrono>
#include <fstream>
#include <sstream>

namespace nazg::bot {

// Embedded doctor script (would be loaded from file in production)
static const char* DOCTOR_SCRIPT = R"(#!/usr/bin/env bash
set -euo pipefail

CPU_WARN=${CPU_WARN:-70}
CPU_CRIT=${CPU_CRIT:-90}
MEM_WARN=${MEM_WARN:-80}
MEM_CRIT=${MEM_CRIT:-90}
DISK_WARN=${DISK_WARN:-85}
DISK_CRIT=${DISK_CRIT:-95}

SERVICES=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --services) SERVICES="$2"; shift 2 ;;
    *) shift ;;
  esac
done

HOSTNAME=$(hostname -f 2>/dev/null || hostname)
TIMESTAMP=$(date +%s)

if command -v uptime &> /dev/null; then
  LOAD_AVG=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | tr -d ',')
  CPU_COUNT=$(nproc 2>/dev/null || echo 1)
  CPU_LOAD_PCT=$(echo "scale=0; ($LOAD_AVG / $CPU_COUNT) * 100" | bc 2>/dev/null || echo "0")
else
  CPU_LOAD_PCT=0
fi

if command -v free &> /dev/null; then
  MEM_INFO=$(free | grep Mem:)
  MEM_TOTAL=$(echo "$MEM_INFO" | awk '{print $2}')
  MEM_USED=$(echo "$MEM_INFO" | awk '{print $3}')
  MEM_USED_PCT=$(echo "scale=0; ($MEM_USED / $MEM_TOTAL) * 100" | bc 2>/dev/null || echo "0")
else
  MEM_USED_PCT=0
fi

if command -v df &> /dev/null; then
  DISK_INFO=$(df -h / | tail -1)
  DISK_USED_PCT=$(echo "$DISK_INFO" | awk '{print $5}' | tr -d '%')
  DISK_FREE=$(echo "$DISK_INFO" | awk '{print $4}')
else
  DISK_USED_PCT=0
  DISK_FREE="unknown"
fi

SERVICE_STATUS="{}"
if [ -n "$SERVICES" ]; then
  IFS=',' read -ra SVC_ARRAY <<< "$SERVICES"
  SERVICE_JSON=""
  for svc in "${SVC_ARRAY[@]}"; do
    svc=$(echo "$svc" | xargs)
    if command -v systemctl &> /dev/null; then
      if systemctl is-active --quiet "$svc"; then
        STATUS="running"
      else
        STATUS="stopped"
      fi
    elif command -v service &> /dev/null; then
      if service "$svc" status &> /dev/null; then
        STATUS="running"
      else
        STATUS="stopped"
      fi
    else
      STATUS="unknown"
    fi
    if [ -n "$SERVICE_JSON" ]; then
      SERVICE_JSON="$SERVICE_JSON,"
    fi
    SERVICE_JSON="$SERVICE_JSON\"$svc\":\"$STATUS\""
  done
  SERVICE_STATUS="{$SERVICE_JSON}"
fi

GATEWAY_REACHABLE="false"
if command -v ip &> /dev/null; then
  GATEWAY=$(ip route | grep default | head -1 | awk '{print $3}')
  if [ -n "$GATEWAY" ] && ping -c 1 -W 2 "$GATEWAY" &> /dev/null; then
    GATEWAY_REACHABLE="true"
  fi
elif command -v route &> /dev/null; then
  GATEWAY=$(route -n | grep '^0.0.0.0' | awk '{print $2}' | head -1)
  if [ -n "$GATEWAY" ] && ping -c 1 -W 2 "$GATEWAY" &> /dev/null; then
    GATEWAY_REACHABLE="true"
  fi
fi

STATUS="ok"
NOTES="[]"

if [ "$CPU_LOAD_PCT" -ge "$CPU_CRIT" ] || [ "$MEM_USED_PCT" -ge "$MEM_CRIT" ] || [ "$DISK_USED_PCT" -ge "$DISK_CRIT" ]; then
  STATUS="critical"
  NOTES="[\"System resources critical\"]"
elif [ "$CPU_LOAD_PCT" -ge "$CPU_WARN" ] || [ "$MEM_USED_PCT" -ge "$MEM_WARN" ] || [ "$DISK_USED_PCT" -ge "$DISK_WARN" ]; then
  STATUS="warning"
  NOTES="[\"System resources elevated\"]"
fi

if echo "$SERVICE_STATUS" | grep -q "stopped"; then
  STATUS="warning"
  NOTES="[\"Some services are not running\"]"
fi

cat <<EOF
{
  "host": "$HOSTNAME",
  "timestamp": $TIMESTAMP,
  "metrics": {
    "cpu_load_pct": $CPU_LOAD_PCT,
    "mem_used_pct": $MEM_USED_PCT,
    "disk_used_pct": $DISK_USED_PCT,
    "disk_free": "$DISK_FREE",
    "services": $SERVICE_STATUS,
    "network": {
      "gateway_reachable": $GATEWAY_REACHABLE
    }
  },
  "status": "$STATUS",
  "notes": $NOTES
}
EOF
)";

std::string DoctorBot::get_script_content() const {
  // Build script with service list from host config
  std::ostringstream script;
  script << DOCTOR_SCRIPT;

  // If services are specified in host config, pass them to script
  if (!host_.services.empty()) {
    script << " --services ";
    for (size_t i = 0; i < host_.services.size(); ++i) {
      if (i > 0) script << ",";
      script << host_.services[i];
    }
  }

  return script.str();
}

RunResult DoctorBot::execute() {
  RunResult result;
  auto start = std::chrono::steady_clock::now();

  if (log_) {
    log_->info("bot.doctor", "Executing Doctor Bot on " + host_.address);
  }

  int exit_code = 0;
  std::string output;
  std::string stderr_output;
  bool executed_via_agent = false;

  if (prefer_agent_ && agent_transport_) {
    if (agent_transport_->execute_script(DOCTOR_SCRIPT, exit_code, output, stderr_output)) {
      executed_via_agent = true;
      if (log_) {
        log_->info("bot.doctor", "Executed via remote agent");
      }
    } else if (log_) {
      log_->warn("bot.doctor", "Agent execution failed, falling back to SSH");
    }
  }

  if (!executed_via_agent) {
    output = ssh_transport_->execute_script(DOCTOR_SCRIPT, exit_code);
  }

  auto end = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  result.exit_code = exit_code;
  result.duration_ms = duration_ms;
  result.stdout_output = output;
  if (!stderr_output.empty()) {
    result.stderr_output = stderr_output;
  }

  // Parse JSON report
  if (exit_code == 0) {
    try {
      ReportData report = parse_report(output);
      result.status = report.status;
      result.json_report = output;

      if (log_) {
        log_->info("bot.doctor", "Health check completed: " + status_to_string(result.status));
      }
    } catch (const std::exception& e) {
      result.status = Status::ERROR;
      result.stderr_output = std::string("Failed to parse report: ") + e.what();

      if (log_) {
        log_->error("bot.doctor", std::string("Failed to parse report: ") + e.what());
      }
    }
  } else {
    result.status = Status::ERROR;
    result.stderr_output = output;  // Use actual output which may contain SSH errors

    if (log_) {
      log_->error("bot.doctor", "Script execution failed with exit code " + std::to_string(exit_code));
    }
  }

  return result;
}

ReportData DoctorBot::parse_report(const std::string& json_output) const {
  // Simple JSON parsing for the report
  // In production, would use a proper JSON parser
  ReportData report;
  report.raw_json = json_output;

  // Extract status field (simple string search)
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

  return report;
}

} // namespace nazg::bot

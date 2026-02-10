#!/usr/bin/env bash
# Nazg Doctor Bot - System Health Diagnostics
# Outputs JSON report to stdout

set -euo pipefail

# Default thresholds (can be overridden by arguments)
CPU_WARN=${CPU_WARN:-70}
CPU_CRIT=${CPU_CRIT:-90}
MEM_WARN=${MEM_WARN:-80}
MEM_CRIT=${MEM_CRIT:-90}
DISK_WARN=${DISK_WARN:-85}
DISK_CRIT=${DISK_CRIT:-95}

# Parse arguments
SERVICES=""
while [[ $# -gt 0 ]]; do
  case $1 in
    --services)
      SERVICES="$2"
      shift 2
      ;;
    *)
      shift
      ;;
  esac
done

# Get hostname
HOSTNAME=$(hostname -f 2>/dev/null || hostname)

# Get timestamp
TIMESTAMP=$(date +%s)

# CPU load (1-minute average)
if command -v uptime &> /dev/null; then
  LOAD_AVG=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | tr -d ',')
  CPU_COUNT=$(nproc 2>/dev/null || echo 1)
  CPU_LOAD_PCT=$(echo "scale=0; ($LOAD_AVG / $CPU_COUNT) * 100" | bc 2>/dev/null || echo "0")
else
  CPU_LOAD_PCT=0
fi

# Memory usage
if command -v free &> /dev/null; then
  MEM_INFO=$(free | grep Mem:)
  MEM_TOTAL=$(echo "$MEM_INFO" | awk '{print $2}')
  MEM_USED=$(echo "$MEM_INFO" | awk '{print $3}')
  MEM_USED_PCT=$(echo "scale=0; ($MEM_USED / $MEM_TOTAL) * 100" | bc 2>/dev/null || echo "0")
else
  MEM_USED_PCT=0
fi

# Disk usage (root filesystem)
if command -v df &> /dev/null; then
  DISK_INFO=$(df -h / | tail -1)
  DISK_USED_PCT=$(echo "$DISK_INFO" | awk '{print $5}' | tr -d '%')
  DISK_FREE=$(echo "$DISK_INFO" | awk '{print $4}')
else
  DISK_USED_PCT=0
  DISK_FREE="unknown"
fi

# Service status
SERVICE_STATUS="{}"
if [ -n "$SERVICES" ]; then
  IFS=',' read -ra SVC_ARRAY <<< "$SERVICES"
  SERVICE_JSON=""
  for svc in "${SVC_ARRAY[@]}"; do
    svc=$(echo "$svc" | xargs)  # trim whitespace
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

# Network connectivity (ping gateway)
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

# Determine overall status
STATUS="ok"
NOTES="[]"

if [ "$CPU_LOAD_PCT" -ge "$CPU_CRIT" ] || [ "$MEM_USED_PCT" -ge "$MEM_CRIT" ] || [ "$DISK_USED_PCT" -ge "$DISK_CRIT" ]; then
  STATUS="critical"
  NOTES="[\"System resources critical\"]"
elif [ "$CPU_LOAD_PCT" -ge "$CPU_WARN" ] || [ "$MEM_USED_PCT" -ge "$MEM_WARN" ] || [ "$DISK_USED_PCT" -ge "$DISK_WARN" ]; then
  STATUS="warning"
  NOTES="[\"System resources elevated\"]"
fi

# Check if any monitored service is down
if echo "$SERVICE_STATUS" | grep -q "stopped"; then
  STATUS="warning"
  NOTES="[\"Some services are not running\"]"
fi

# Output JSON report
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

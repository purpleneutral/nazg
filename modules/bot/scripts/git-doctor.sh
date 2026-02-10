#!/usr/bin/env bash
# Git Doctor Bot - Git server infrastructure health check
# Checks cgit/gitea installation, services, and repository health

set -euo pipefail

# Thresholds
DISK_WARN=${DISK_WARN:-85}
DISK_CRIT=${DISK_CRIT:-95}

# Parse arguments
GIT_TYPE="cgit"
REPO_PATH="/srv/git"
CONFIG_PATH="/etc/cgitrc"

while [[ $# -gt 0 ]]; do
  case $1 in
    --type) GIT_TYPE="$2"; shift 2 ;;
    --repo-path) REPO_PATH="$2"; shift 2 ;;
    --config-path) CONFIG_PATH="$2"; shift 2 ;;
    *) shift ;;
  esac
done

HOSTNAME=$(hostname -f 2>/dev/null || hostname)
TIMESTAMP=$(date +%s)

# ===== Git Server Detection =====
GIT_SERVER_INSTALLED="false"
GIT_SERVER_VERSION=""
GIT_SERVER_TYPE="$GIT_TYPE"

if command -v cgit &> /dev/null; then
  GIT_SERVER_INSTALLED="true"
  GIT_SERVER_TYPE="cgit"
  # cgit doesn't have --version, try to get it from package manager
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
TOTAL_SIZE_BYTES=0

if [ -d "$REPO_PATH" ]; then
  # Count .git directories
  REPO_COUNT=$(find "$REPO_PATH" -maxdepth 1 -type d -name "*.git" 2>/dev/null | wc -l)

  # Calculate total size in MB
  if [ "$REPO_COUNT" -gt 0 ]; then
    TOTAL_SIZE_CANDIDATE=$(du -sb "$REPO_PATH" 2>/dev/null | awk 'NR==1 {print $1; exit}')
    if [[ "$TOTAL_SIZE_CANDIDATE" =~ ^[0-9]+$ ]]; then
      TOTAL_SIZE_BYTES=$TOTAL_SIZE_CANDIDATE
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
WEB_UI_PORT=80

# Check if nginx is listening on port 80
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

# Check if git-http-backend is available
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

#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
NAZG_BIN="${NAZG_BIN:-$BUILD_DIR/nazg}"
DO_BUILD=0
VERBOSE=0
KEEP_ARTIFACTS=0

usage() {
  cat <<'USAGE'
Usage: tests/smoke.sh [options]

Options:
  --build           Run CMake configure and build before testing
  --bin PATH        Path to nazg executable (default: $BUILD_DIR/nazg)
  -v, --verbose     Print command output even when tests pass
  --keep-artifacts  Keep previous run artifacts (default removes)
  -h, --help        Show this help message
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build)
      DO_BUILD=1
      ;;
    --bin)
      shift
      [[ $# -gt 0 ]] || { echo "error: --bin requires a path" >&2; exit 1; }
      NAZG_BIN="$1"
      ;;
    -v|--verbose)
      VERBOSE=1
      ;;
    --keep-artifacts)
      KEEP_ARTIFACTS=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

if (( DO_BUILD )); then
  cmake -S "$ROOT" -B "$BUILD_DIR"
  cmake --build "$BUILD_DIR"
fi

if [[ ! -x "$NAZG_BIN" ]]; then
  echo "error: nazg binary not found or not executable at: $NAZG_BIN" >&2
  echo "hint: run --build or build.sh first" >&2
  exit 1
fi

TEST_ROOT="$ROOT/.tmp/nazg-smoke"
ARTIFACT_DIR="$TEST_ROOT/artifacts"

if (( ! KEEP_ARTIFACTS )); then
  rm -rf "$TEST_ROOT"
fi

mkdir -p "$ARTIFACT_DIR"

STATE_DIR="$TEST_ROOT/state"
DATA_DIR="$TEST_ROOT/data"
CONFIG_DIR="$TEST_ROOT/config"
CACHE_DIR="$TEST_ROOT/cache"
LOG_DIR="$STATE_DIR/nazg/logs"

mkdir -p "$LOG_DIR" "$DATA_DIR/nazg" "$CONFIG_DIR/nazg" "$CACHE_DIR/nazg"

export XDG_STATE_HOME="$STATE_DIR"
export XDG_DATA_HOME="$DATA_DIR"
export XDG_CONFIG_HOME="$CONFIG_DIR"
export XDG_CACHE_HOME="$CACHE_DIR"
export NAZG_LOG_CONSOLE=1

if [[ ${NAZG_TEST_FAKE_SSHPASS:-1} -ne 0 ]]; then
  FAKE_BIN_DIR="$TEST_ROOT/fake-bin"
  mkdir -p "$FAKE_BIN_DIR"
  cat <<'EOF' > "$FAKE_BIN_DIR/sshpass"
#!/usr/bin/env bash
echo "[nazg-test] stub sshpass invoked" >&2
exit 0
EOF
  chmod +x "$FAKE_BIN_DIR/sshpass"
  export PATH="$FAKE_BIN_DIR:$PATH"
fi

PASS=0
FAIL=0
SKIP=0
SUMMARY=()

AGENT_PID=""
TEST_AGENT_PORT="${TEST_AGENT_PORT:-7070}"

cleanup() {
  if [[ -n "$AGENT_PID" ]] && kill -0 "$AGENT_PID" 2>/dev/null; then
    kill "$AGENT_PID" >/dev/null 2>&1 || true
    wait "$AGENT_PID" 2>/dev/null || true
  fi
  AGENT_PID=""
}

trap cleanup EXIT

start_agent() {
  local log_file="$ARTIFACT_DIR/agent.log"
  rm -f "$log_file"
  NAZG_AGENT_ADDR=127.0.0.1 NAZG_AGENT_PORT="$TEST_AGENT_PORT" \
    "$BUILD_DIR/nazg-agent" >"$log_file" 2>&1 &
  AGENT_PID=$!

  for _ in $(seq 1 50); do
    if ! kill -0 "$AGENT_PID" 2>/dev/null; then
      return 1
    fi
    if grep -q "nazg-agent listening" "$log_file" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done

  return 1
}

stop_agent() {
  if [[ -n "$AGENT_PID" ]] && kill -0 "$AGENT_PID" 2>/dev/null; then
    kill "$AGENT_PID" >/dev/null 2>&1 || true
    wait "$AGENT_PID" 2>/dev/null || true
  fi
  AGENT_PID=""
}

skip_case() {
  local name="$1"
  local reason="$2"
  ((SKIP+=1))
  SUMMARY+=("[SKIP] $name -> $reason")
  echo "[SKIP] $name -> $reason"
}

slugify() {
  echo "$1" | tr '[:upper:]' '[:lower:]' | tr ' ' '-' | tr -cd 'a-z0-9-_' \
    | sed -e 's/--*/-/g' -e 's/^-//' -e 's/-$//'
}

run_case() {
  local name="$1"; shift
  local expected_exit="$1"; shift
  local cmd="$1"; shift

  local args=()
  while [[ $# -gt 0 && "$1" != "--" ]]; do
    args+=("$1")
    shift
  done
  if [[ $# -gt 0 && "$1" == "--" ]]; then
    shift
  fi

  local patterns=()
  while [[ $# -gt 0 ]]; do
    patterns+=("$1")
    shift
  done

  local idx
  printf -v idx "%02d" "${#SUMMARY[@]}"
  local slug
  slug=$(slugify "$name")
  local log_file="$ARTIFACT_DIR/${idx}-${slug}.log"

  local output
  local exit_code
  set +e
  output="$("$cmd" "${args[@]}" 2>&1)"
  exit_code=$?
  set -e

  printf '%s
' "$output" > "$log_file"

  local status="PASS"
  local reason=""

  if [[ $exit_code -ne $expected_exit ]]; then
    status="FAIL"
    reason="exit $exit_code (expected $expected_exit)"
  else
    for pat in "${patterns[@]}"; do
      if ! grep -Fq -- "$pat" <<<"$output"; then
        status="FAIL"
        reason="missing '$pat'"
        break
      fi
    done
  fi

  if [[ $status == "PASS" ]]; then
    ((PASS+=1))
    SUMMARY+=("[PASS] $name")
    echo "[PASS] $name"
    if (( VERBOSE )); then
      echo "$output"
      echo
    fi
  else
    ((FAIL+=1))
    SUMMARY+=("[FAIL] $name -> $reason (see $log_file)")
    echo "[FAIL] $name -> $reason"
    echo "Command: $cmd ${args[*]}"
    echo "Log: $log_file"
    if (( VERBOSE )); then
      echo "$output"
    fi
  fi
}

PROJECT_PATH="$ROOT"

run_case "nazg --help" 0 "$NAZG_BIN" "--help" -- \
  "usage:" \
  "Run 'nazg' with no arguments for interactive assistant mode."

run_case "commands table filter" 0 "$NAZG_BIN" "commands" "--only=status" -- \
  "COMMANDS" \
  "status              Show project summary for current directory"

run_case "commands json filter" 0 "$NAZG_BIN" "commands" "--json" "--only=status" -- \
  '"name": "status"'

run_case "status without git" 0 "$NAZG_BIN" "status" -- \
  "Nazg Project Status" \
  "Git       : not a repository" \
  "Tip: Run 'nazg' for interactive assistance."

run_case "build_status fresh" 0 "$NAZG_BIN" "build_status" -- \
  "Status: Not detected yet (run 'nazg build' first)"

run_case "build_facts fresh" 0 "$NAZG_BIN" "build_facts" -- \
  "No facts found (run 'nazg build' first)"

run_case "why analysis" 0 "$NAZG_BIN" "why" -- \
  "Analyzing project..." \
  "Project: $PROJECT_PATH" \
  "Run 'nazg build' to execute this plan"

run_case "git-status fails outside repo" 1 "$NAZG_BIN" "git-status" -- \
  "Not a git repository"

run_case "bot help" 0 "$NAZG_BIN" "bot" "--help" -- \
  "Usage:" \
  "Available subcommands"

if start_agent; then
  run_case "bot doctor (agent)" 0 "$NAZG_BIN" "bot" "doctor" \
    "--host" "127.0.0.1" "--label" "local-agent" "--agent-port" "$TEST_AGENT_PORT" -- \
  "Bot execution completed:" \
  '"status": "ok"'
  stop_agent

  last_idx=$(( ${#SUMMARY[@]} - 1 ))
  if (( last_idx >= 0 )) && [[ ${SUMMARY[last_idx]} == "[PASS] bot doctor (agent)"* ]]; then
    run_case "bot list after agent run" 0 "$NAZG_BIN" "bot" "list" -- \
      "Recent bot runs:" \
      "doctor" \
      "local-agent"

    run_case "bot report after agent run" 0 "$NAZG_BIN" "bot" "report" "doctor" \
      "--host" "local-agent" -- \
      "Latest doctor report for local-agent" \
      '"status": "ok"'
  else
    skip_case "bot list after agent run" "bot doctor (agent) did not pass"
    skip_case "bot report after agent run" "bot doctor (agent) did not pass"
  fi
else
  skip_case "bot doctor (agent)" "agent runtime unavailable (socket permissions?)"
  skip_case "bot list after agent run" "agent runtime unavailable (socket permissions?)"
  skip_case "bot report after agent run" "agent runtime unavailable (socket permissions?)"
fi

run_case "bot hosts empty" 0 "$NAZG_BIN" "bot" "hosts" -- \
  "No bot hosts registered yet."

run_case "bot history empty" 0 "$NAZG_BIN" "bot" "history" -- \
  "No bot runs found for the specified criteria."

echo
if (( SKIP > 0 )); then
  echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
else
  echo "Results: $PASS passed, $FAIL failed"
fi
for line in "${SUMMARY[@]}"; do
  echo "  $line"
done

echo "Artifacts saved to: $ARTIFACT_DIR"

if (( FAIL )); then
  exit 1
fi

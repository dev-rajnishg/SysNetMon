#!/usr/bin/env bash
set -euo pipefail

SERVER_HOST="${SERVER_HOST:-127.0.0.1}"
SERVER_PORT="${SERVER_PORT:-9090}"
AGENT_COUNT="${AGENT_COUNT:-2}"
INTERVAL_SECONDS="${INTERVAL_SECONDS:-3}"
DASHBOARD_PORT="${DASHBOARD_PORT:-5000}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

cmake -S . -B build
cmake --build build --config Release

SERVER_BIN="build/sysnetmon-server"
AGENT_BIN="build/sysnetmon-agent"

if [[ ! -x "$SERVER_BIN" || ! -x "$AGENT_BIN" ]]; then
  echo "Native binaries not found after build"
  exit 1
fi

python3 -m venv .venv
source .venv/bin/activate
pip install -r python/dashboard/requirements.txt

"$SERVER_BIN" "$SERVER_PORT" > /tmp/sysnetmon-server.log 2>&1 &
SERVER_PID=$!
echo "$SERVER_PID" > /tmp/sysnetmon-server.pid

sleep 1

AGENT_PIDS=()
for i in $(seq 1 "$AGENT_COUNT"); do
  name="agent-${i}"
  "$AGENT_BIN" "$SERVER_HOST" "$SERVER_PORT" "$name" "$INTERVAL_SECONDS" > "/tmp/sysnetmon-agent-${i}.log" 2>&1 &
  AGENT_PIDS+=("$!")
done

export MONITOR_SERVER_HOST="$SERVER_HOST"
export MONITOR_SERVER_PORT="$SERVER_PORT"

(cd python/dashboard && flask --app app run --host=0.0.0.0 --port="$DASHBOARD_PORT") > /tmp/sysnetmon-dashboard.log 2>&1 &
DASHBOARD_PID=$!

printf "%s\n" "${AGENT_PIDS[@]}" > /tmp/sysnetmon-agent.pids
echo "$DASHBOARD_PID" > /tmp/sysnetmon-dashboard.pid

echo "SysNetMon is running"
echo "Dashboard: http://localhost:${DASHBOARD_PORT}"
echo "Server PID: ${SERVER_PID}"
echo "Agent PIDs: ${AGENT_PIDS[*]}"
echo "Dashboard PID: ${DASHBOARD_PID}"
echo "Run scripts/stop-local.sh to stop all processes"
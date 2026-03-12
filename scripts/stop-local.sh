#!/usr/bin/env bash
set -euo pipefail

for pid_file in /tmp/sysnetmon-server.pid /tmp/sysnetmon-dashboard.pid; do
  if [[ -f "$pid_file" ]]; then
    pid="$(cat "$pid_file")"
    kill "$pid" 2>/dev/null || true
    rm -f "$pid_file"
  fi
done

if [[ -f /tmp/sysnetmon-agent.pids ]]; then
  while IFS= read -r pid; do
    kill "$pid" 2>/dev/null || true
  done < /tmp/sysnetmon-agent.pids
  rm -f /tmp/sysnetmon-agent.pids
fi

echo "Stopped SysNetMon server, agents, and dashboard processes (if running)."
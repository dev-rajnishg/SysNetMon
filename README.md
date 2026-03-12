# SysNetMon

## CI Status

[![Cross-Platform CI](https://github.com/dev-rajnishg/SysNetMon/actions/workflows/ci.yml/badge.svg)](https://github.com/dev-rajnishg/SysNetMon/actions/workflows/ci.yml)

SysNetMon is a distributed system monitor built as an infra-focused monorepo for systems and networking roles. It combines a low-level C++ TCP server, cross-platform C++ monitoring daemons, a Python Flask dashboard with Plotly, and AWS alert delivery using S3 and SNS.

The native code now targets Windows, macOS, and Linux. The socket layer uses portable `select`-based TCP handling, while metric collection switches to each platform's native APIs: `/proc` on Linux, Mach and `getifaddrs` on macOS, and Win32 or IP Helper APIs on Windows.

## Architecture

- `cpp/server/main.cpp`: `select`-based TCP event server that accepts 10+ agent and dashboard connections on Windows, macOS, and Linux.
- `cpp/client/main.cpp`: cross-platform monitoring daemon that uses native OS metric APIs.
- `python/dashboard/app.py`: Flask dashboard with REST endpoints for commands, chat overlay, and alert upload actions.
- `python/dashboard/services.py`: background socket bridge to the C++ server plus AWS S3/SNS publishing through Boto3.
- `cpp/include/platform.hpp`: shared socket compatibility layer for Winsock and POSIX.
- `cpp/include/metrics.hpp`: per-OS CPU, memory, disk, and network collectors.

The wire protocol is newline-delimited JSON over raw TCP. Agents register with the server, send periodic metrics, and dashboards subscribe to live metric, chat, snapshot, and alert events.

## Features

- C++ server using portable sockets and `select` for multi-client handling.
- Windows, macOS, and Linux metric collectors for CPU, memory, disk, and network throughput.
- Server-side commands such as `/alert CPU>80%`, `/alert MEMORY>70%`, `/listalerts`, and `/clients`.
- Chat overlay shared through the same socket event stream.
- Plotly charts for CPU, memory, and network activity across hosts.
- Automatic AWS uploads for alert events to S3 and optional SNS notification fan-out.
- EC2-friendly deployment path with Docker and a simple root Makefile.

## Native Build

### CMake on Windows, macOS, or Linux

```bash
cmake -S . -B build
cmake --build build --config Release
```

The output binaries are:

- `build/sysnetmon-server` on Linux and macOS
- `build/Release/sysnetmon-server.exe` on multi-config Windows generators
- `build/sysnetmon-agent` on Linux and macOS
- `build/Release/sysnetmon-agent.exe` on multi-config Windows generators

## Quick Start Scripts

- Windows PowerShell: `./scripts/run-local.ps1`
- macOS or Linux: `./scripts/run-local.sh`

These scripts build native binaries, set up the dashboard Python environment, launch the server plus multiple agents, and start Flask.

Stop everything with:

- Windows PowerShell: `./scripts/stop-local.ps1`
- macOS or Linux: `./scripts/stop-local.sh`

### Makefile on Linux or macOS

1. Build the native binaries:

```bash
make server client
```

2. Start the C++ server:

```bash
./bin/sysnetmon-server 9090
```

3. Start one or more monitoring agents on the same machine:

```bash
./bin/sysnetmon-agent 127.0.0.1 9090 agent-1 3
./bin/sysnetmon-agent 127.0.0.1 9090 agent-2 3
./bin/sysnetmon-agent 127.0.0.1 9090 agent-3 3
```

To run an agent as a background daemon on Linux:

```bash
./bin/sysnetmon-agent 127.0.0.1 9090 edge-node-1 5 --daemon
```

4. Install and launch the Flask dashboard:

```bash
cd python/dashboard
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
flask --app app run --host=0.0.0.0 --port=5000
```

5. Open `http://localhost:5000`, add a rule like `/alert CPU>10%`, and watch the alert feed and AWS status panel.

## Run the Server and Agents

Linux or macOS:

```bash
./build/sysnetmon-server 9090
./build/sysnetmon-agent 127.0.0.1 9090 agent-1 3
./build/sysnetmon-agent 127.0.0.1 9090 agent-2 3
```

Windows PowerShell:

```powershell
.\build\Release\sysnetmon-server.exe 9090
.\build\Release\sysnetmon-agent.exe 127.0.0.1 9090 agent-1 3
.\build\Release\sysnetmon-agent.exe 127.0.0.1 9090 agent-2 3
```

Agents still support `--daemon` on Unix-like systems. On Windows, run them as standard background processes or services.

## Dashboard Run

```bash
cd python/dashboard
python -m venv .venv
pip install -r requirements.txt
flask --app app run --host=0.0.0.0 --port=5000
```

Open `http://localhost:5000`, add a rule like `/alert CPU>10%`, and watch the alert feed and AWS status panel.

## Docker Run

```bash
docker compose up --build
```

This launches the C++ server on port `9090` and the dashboard on port `5000`.

## CI Matrix

GitHub Actions workflow `.github/workflows/ci.yml` validates the repo on:

- Windows
- macOS
- Linux

It includes a lightweight Linux end-to-end health check that starts the C++ server and one agent, then validates that a dashboard socket client receives a snapshot containing live metrics.
- Linux

CI builds `sysnetmon-server` and `sysnetmon-agent` with CMake on each OS, and runs Flask dashboard dependency plus import checks.

## AWS Configuration

Set any of the following environment variables before starting the dashboard or Docker stack:

- `AWS_REGION=ap-south-1`
- `ALERT_S3_BUCKET=sysnetmon-alert-bucket`
- `ALERT_S3_PREFIX=alerts/`
- `ALERT_SNS_TOPIC_ARN=arn:aws:sns:ap-south-1:123456789012:sysnetmon-alerts`

When an alert event arrives, the dashboard uploads the JSON payload to S3 and optionally publishes the same payload to SNS.

## EC2 / VPC Deployment Notes

1. Launch an Ubuntu EC2 instance inside your VPC.
2. Open inbound security group rules for TCP `9090` from agent subnets and TCP `5000` from your admin IP.
3. Install Docker or build natively with `g++` and Python 3.12.
4. Run the C++ server on the EC2 host and point dashboard and clients to the EC2 private IP.
5. Attach an IAM role with `s3:PutObject` and `sns:Publish` permissions if using AWS uploads.

## Demo Checklist for GitHub

- Show the server accepting multiple localhost agents.
- Show the dashboard updating charts live.
- Send `/alert CPU>10%` from the UI and trigger an alert.
- Show the latest alert object in S3 or the SNS notification receipt.
- Include a short demo video in the repository README or release notes.

## Cross-Platform Notes

- Linux uses `/proc` and `statvfs`.
- macOS uses Mach host statistics, `sysctl`, `getifaddrs`, and `statvfs`.
- Windows uses `GetSystemTimes`, `GlobalMemoryStatusEx`, `GetDiskFreeSpaceEx`, and `GetIfTable`.
- The TCP wire protocol and Flask dashboard are unchanged across platforms.

## Why This Fits Systems / Networking Roles

The project demonstrates socket programming, Linux internals, multi-client event handling, cloud integration, live monitoring, and service-style deployment. That combination maps cleanly to entry-level infrastructure, networking, and system engineering expectations.
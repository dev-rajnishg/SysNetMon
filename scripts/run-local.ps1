param(
    [string]$ServerHost = "127.0.0.1",
    [int]$ServerPort = 9090,
    [int]$AgentCount = 2,
    [int]$IntervalSeconds = 3,
    [int]$DashboardPort = 5000
)

$ErrorActionPreference = "Stop"

Set-Location (Join-Path $PSScriptRoot "..")

if (!(Test-Path "build")) {
    cmake -S . -B build
}
cmake --build build --config Release

$serverExe = if (Test-Path "build/Release/sysnetmon-server.exe") {
    "build/Release/sysnetmon-server.exe"
} else {
    "build/sysnetmon-server.exe"
}

$agentExe = if (Test-Path "build/Release/sysnetmon-agent.exe") {
    "build/Release/sysnetmon-agent.exe"
} else {
    "build/sysnetmon-agent.exe"
}

if (!(Test-Path $serverExe) -or !(Test-Path $agentExe)) {
    throw "Native binaries not found after build"
}

if (!(Test-Path ".venv")) {
    python -m venv .venv
}

$pythonExe = ".venv/Scripts/python.exe"
& $pythonExe -m pip install -r python/dashboard/requirements.txt

Write-Host "Starting server on port $ServerPort"
$server = Start-Process -FilePath $serverExe -ArgumentList @("$ServerPort") -PassThru

Start-Sleep -Seconds 1

$agents = @()
for ($i = 1; $i -le $AgentCount; $i++) {
    $name = "agent-win-$i"
    Write-Host "Starting $name"
    $agent = Start-Process -FilePath $agentExe -ArgumentList @($ServerHost, "$ServerPort", $name, "$IntervalSeconds") -PassThru
    $agents += $agent
}

$env:MONITOR_SERVER_HOST = $ServerHost
$env:MONITOR_SERVER_PORT = "$ServerPort"

Write-Host "Starting Flask dashboard on port $DashboardPort"
$dashboard = Start-Process -FilePath $pythonExe -ArgumentList @("-m", "flask", "--app", "app", "run", "--host=0.0.0.0", "--port=$DashboardPort") -WorkingDirectory "python/dashboard" -PassThru

Write-Host ""
Write-Host "SysNetMon is running."
Write-Host "Dashboard: http://localhost:$DashboardPort"
Write-Host "Server PID: $($server.Id)"
Write-Host "Agent PIDs: $($agents.Id -join ', ')"
Write-Host "Dashboard PID: $($dashboard.Id)"
Write-Host "Use scripts/stop-local.ps1 to stop all started processes by name."
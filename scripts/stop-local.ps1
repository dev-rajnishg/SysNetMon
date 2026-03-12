$ErrorActionPreference = "Continue"

Get-Process sysnetmon-server -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process sysnetmon-agent -ErrorAction SilentlyContinue | Stop-Process -Force

$dashboard = Get-CimInstance Win32_Process |
    Where-Object {
        $_.Name -match "python" -and $_.CommandLine -match "flask" -and $_.CommandLine -match "python/dashboard"
    }
foreach ($process in $dashboard) {
    Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
}

Write-Host "Stopped SysNetMon server, agents, and dashboard processes (if running)."
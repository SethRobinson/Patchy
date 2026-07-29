# Frees a local server port by stopping a node process listening on it, for
# scripts\release\start-local-wasm-server.bat (and anything else that wants to
# restart serve.mjs without cleaning up first).
#
# Only our own server is ever stopped: the listener must be a node process
# whose command line runs serve.mjs. A bare process-name check is not enough,
# because other applications ship their own node.exe (Adobe Creative Cloud has
# one on a typical Patchy dev box), and a force-kill has to be sure of its
# target. Anything else holding the port is reported instead, which is also
# the honest answer when the command line cannot be read.
#
# This lives in its own file rather than inline in the batch file on purpose:
# cmd's `for /f ... in (`...`)` parser trips over literal parentheses inside
# the backquoted command, which PowerShell needs for subexpressions and array
# subscripts, so an inline one-liner fails to parse ("( was unexpected at this
# time").
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File free-server-port.ps1 -Port 8973
#
# Prints exactly one status line for the caller to branch on, and always exits
# 0 so the caller reads the status rather than an exit code:
#   free              nothing was listening
#   stopped:<pids>    a serve.mjs server was listening and has been stopped
#   busy:<name> PID N something else holds the port; nothing was stopped
#   failed:PID N      a serve.mjs server was found but could not be stopped
param([Parameter(Mandatory = $true)][int]$Port)

$ErrorActionPreference = 'Stop'

$listeners = @(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
if ($listeners.Count -eq 0) {
  'free'
  exit 0
}

$stopped = @()
foreach ($owner in @($listeners.OwningProcess | Sort-Object -Unique)) {
  $process = Get-Process -Id $owner -ErrorAction SilentlyContinue
  if (-not $process) {
    # The listener died between the enumeration and here; the port is free.
    continue
  }
  $commandLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $owner" -ErrorAction SilentlyContinue).CommandLine
  if ($process.ProcessName -ne 'node' -or $commandLine -notlike '*serve.mjs*') {
    "busy:$($process.ProcessName) PID $owner"
    exit 0
  }
  try {
    Stop-Process -Id $owner -Force -ErrorAction Stop
    $stopped += $owner
  } catch {
    "failed:PID $owner"
    exit 0
  }
}

if ($stopped.Count -gt 0) {
  # Stop-Process returns before the socket is released; wait for the port to
  # actually clear so the caller's server cannot still hit EADDRINUSE.
  for ($attempt = 0; $attempt -lt 50; $attempt++) {
    if (@(Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue).Count -eq 0) {
      break
    }
    Start-Sleep -Milliseconds 100
  }
  "stopped:$($stopped -join ', ')"
} else {
  'free'
}
exit 0

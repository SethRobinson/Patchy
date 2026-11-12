<#
.SYNOPSIS
Self-test for watch-release.ps1. Builds a scratch marker folder and a stand-in launcher under
%TEMP%\patchy-watch-<random>, runs every case with the watcher's output captured through a pipe
(the way a harness runs it), and checks the exit codes, the chains and that a launch never
holds the watch. Takes about a minute. Exit 0 when all cases pass.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\watch-release.test.ps1
#>
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'watch-release.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ('patchy-watch-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$logs = Join-Path $root 'logs'
$launcher = Join-Path $root 'launcher.bat'
$failures = New-Object System.Collections.Generic.List[string]
$slowSeconds = 45

function Check([bool]$ok, [string]$what) {
  if ($ok) { "  ok    $what" } else { "  FAIL  $what"; $script:failures.Add($what) }
}
function Run-Watch([string[]]$extra) {
  $ps = (Get-Process -Id $PID).Path
  # A case that goes wrong must fail in two minutes, not sit out the 25-minute default.
  if ($extra -notcontains '-CheckInMinutes') { $extra += '-CheckInMinutes', '2' }
  $clock = [Diagnostics.Stopwatch]::StartNew()
  $out = & $ps -NoProfile -ExecutionPolicy Bypass -File $script -LogDir $logs -Launcher $launcher -PollSeconds 1 @extra 2>&1 |
    ForEach-Object { "$_" }
  return @{ code = $LASTEXITCODE; out = ($out -join "`n"); seconds = $clock.Elapsed.TotalSeconds }
}
function Wait-Marker([string]$target, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while (-not (Test-Path -LiteralPath (Join-Path $logs "$target.exit")) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
  return Test-Path -LiteralPath (Join-Path $logs "$target.exit")
}

try {
  New-Item -ItemType Directory -Force $logs | Out-Null
  # The stand-in launcher starts its own console and returns, as release-all-automated.bat does.
  # A target named slow-* runs for $slowSeconds seconds; fail-* ends with exit=1.
  Set-Content -LiteralPath $launcher -Encoding ascii -Value @"
@echo off
set "T=%~1"
set "SECS=0"
set "CODE=0"
if /i "%T:~0,5%"=="slow-" set "SECS=$slowSeconds"
if /i "%T:~0,5%"=="fail-" set "CODE=1"
start "patchy watch test %T%" /min cmd /c ""%~dp0worker.bat" %T% %SECS% %CODE%"
exit /b 0
"@
  Set-Content -LiteralPath (Join-Path $root 'worker.bat') -Encoding ascii -Value @'
@echo off
> "%~dp0logs\%~1.log" echo started
if not "%~2"=="0" ping -n %~2 127.0.0.1 >nul
> "%~dp0logs\%~1.exit" echo exit=%~3
'@

  'a finished target ends the watch with exit 0'
  $r = Run-Watch @('-Launch', 'quick-a', '-Wait', 'quick-a')
  Check ($r.code -eq 0) "exit 0 (got $($r.code))"
  Check ($r.out -match 'FINISHED: quick-a \(exit=0\)') 'names the finished target'

  'a failed target ends the watch with exit 2'
  $r = Run-Watch @('-Launch', 'fail-b', '-Wait', 'fail-b')
  Check ($r.code -eq 2) "exit 2 (got $($r.code))"
  Check ($r.out -match 'FAILED: fail-b \(exit=1\)') 'names the failed target'

  'a launch does not hold the watch: the check-in comes back while the target still runs'
  $r = Run-Watch @('-Launch', 'slow-c', '-Wait', 'slow-c', '-CheckInMinutes', '0.1')
  Check ($r.code -eq 5) "exit 5 (got $($r.code))"
  Check ($r.seconds -lt 25) ("returned in {0:N0} s, well inside the target's $slowSeconds s" -f $r.seconds)
  Check ($r.out -match 'CHECK-IN') 'prints the check-in line'
  Check ($r.out -match 'slow-c\s+running') 'the table shows the target as running'
  Check (-not (Test-Path -LiteralPath (Join-Path $logs 'slow-c.exit'))) 'the target had not finished'

  'a running target whose logs stop growing ends the watch with exit 3'
  $r = Run-Watch @('-Launch', 'slow-i', '-Wait', 'slow-i', '-StallMinutes', '0.05', '-CheckInMinutes', '5')
  Check ($r.code -eq 3) "exit 3 (got $($r.code))"
  Check ($r.out -match 'STALL') 'prints the stall line'

  'what had finished before the watch began is not news'
  $r = Run-Watch @('-Wait', 'quick-a', '-CheckInMinutes', '0.05')
  Check ($r.code -eq 5) "exit 5 (got $($r.code))"

  'a chain starts the next target after a success'
  $r = Run-Watch @('-Wait', 'quick-e', '-Chain', 'quick-a>quick-e')
  Check ($r.code -eq 0) "exit 0 (got $($r.code))"
  Check ($r.out -match 'launched quick-e') 'launched the chained target'
  Check ($r.out -match 'FINISHED: quick-e \(exit=0\)') 'reports the chained target'

  'a chain does not fire after a failure'
  $r = Run-Watch @('-Wait', 'quick-f', '-Chain', 'fail-b>quick-f', '-CheckInMinutes', '0.05')
  Check ($r.code -eq 5) "exit 5 (got $($r.code))"
  Check (-not (Test-Path -LiteralPath (Join-Path $logs 'quick-f.log'))) 'the chained target never started'

  'a chain with two sources waits for both'
  $r = Run-Watch @('-Wait', 'quick-g', '-Chain', 'quick-a+slow-c>quick-g', '-CheckInMinutes', '0.05')
  Check ($r.code -eq 5) "exit 5 while slow-c runs (got $($r.code))"
  Check (-not (Test-Path -LiteralPath (Join-Path $logs 'quick-g.log'))) 'the chained target waited'
  Check (Wait-Marker 'slow-c' ($slowSeconds + 20)) 'slow-c finished'
  $r = Run-Watch @('-Wait', 'quick-g', '-Chain', 'quick-a+slow-c>quick-g')
  Check ($r.code -eq 0 -and $r.out -match 'FINISHED: quick-g') "fires once both ended (got $($r.code))"

  'a log belongs to the longest target name it starts with'
  [IO.File]::WriteAllBytes((Join-Path $logs 'verify-windows.log'), (New-Object byte[] 10))
  [IO.File]::WriteAllBytes((Join-Path $logs 'verify-windows-ui.log'), (New-Object byte[] 100))
  [IO.File]::WriteAllBytes((Join-Path $logs 'verify-windows-arm64.log'), (New-Object byte[] 5))
  [IO.File]::WriteAllBytes((Join-Path $logs 'verify-windows-arm64-suites.log'), (New-Object byte[] 1000))
  $r = Run-Watch @('-Wait', 'verify-windows,verify-windows-arm64', '-CheckInMinutes', '0.02')
  Check ($r.out -match 'verify-windows\s+running\s+logs\s+110 bytes') 'verify-windows counts its own two logs'
  Check ($r.out -match 'verify-windows-arm64\s+running\s+logs\s+1005 bytes') 'verify-windows-arm64 counts its own two logs'

  Check (Wait-Marker 'slow-i' ($slowSeconds + 20)) 'slow-i finished (nothing left running before cleanup)'
}
finally {
  if ($root -like '*\patchy-watch-*' -and (Test-Path -LiteralPath $root)) {
    Remove-Item -LiteralPath $root -Recurse -Force
  }
}
if ($failures.Count) { "$($failures.Count) check(s) failed"; exit 1 }
'all checks passed'
exit 0

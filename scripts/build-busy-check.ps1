<#
The machine-wide build lock behind scripts\run-throttled.bat. For a `cmake --build`, the
batch file puts its own argument line in PATCHY_THROTTLED_ARGS and runs this script, which:

  1. takes the named mutex Global\PatchyLocalBuild without waiting. When another build holds
     it, the script says which wrapper holds it and exits 3 without building;
  2. with the mutex held, still refuses (exit 3) when a build that did not come through the
     lock is compiling (a `cmake --build`, ninja or MSBuild process from an older checkout's
     wrapper or an IDE);
  3. runs run-throttled.bat again with the same argument line and PATCHY_BUILD_LOCK_HELD=1,
     which makes it start the build at below-normal priority as before, waits for it, and
     exits with its exit code. The mutex is held for the whole build and released when this
     process ends, however it ends: Windows hands an abandoned mutex to the next taker.

Checking for a running build and then starting one, as this script once did, left a window
in which two sessions both saw an idle machine and both started (October 2026). A mutex
cannot be taken twice, so of two builds started at the same moment exactly one runs. A mutex
rather than a lock file: a file handle can be inherited by the build's children (cl.exe's
mspdbsrv.exe outlives the build), which would keep the lock after the build ended.

Two builds at once on the dev box (several AI sessions share it) take twice as long each
and can starve the machine. The caller then waits, or builds on the Windows offload host
instead: scripts\remote\remote-build.ps1 -Target windows probes the configured hosts and
builds on an idle one. The local build\release stays the release gate.

PATCHY_ALLOW_CONCURRENT_BUILD=1 makes run-throttled.bat skip this script entirely (for when
Seth says to build anyway).
#>
$ErrorActionPreference = 'Stop'

function Get-RunningBuilds {
  $self = $PID
  @(Get-CimInstance Win32_Process | Where-Object {
      $_.ProcessId -ne $self -and (
        $_.Name -ieq 'ninja.exe' -or $_.Name -ieq 'MSBuild.exe' -or
        ($_.Name -ieq 'cmake.exe' -and $_.CommandLine -match '(^|\s)--build(\s|$)'))
    })
}

function Write-Busy([string]$Why, $Builds) {
  $lines = @($Builds | Sort-Object CreationDate | ForEach-Object {
      $started = if ($_.CreationDate) { $_.CreationDate.ToString('HH:mm:ss') } else { '?' }
      "  PID $($_.ProcessId) $($_.Name), started $started"
    })
  if (-not $lines.Count) { $lines = @('  (no compiler process yet: it is still starting)') }
  [Console]::Error.WriteLine(@"
run-throttled: $Why
$($lines -join [Environment]::NewLine)
Not starting a second one beside it. Wait for it to finish, or build and test on the
Windows offload host meanwhile:
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\remote\remote-build.ps1 -Target windows
(the local build\release stays the release gate, so build here once this box is free).
Set PATCHY_ALLOW_CONCURRENT_BUILD=1 only when told to build here anyway.
"@)
}

$commandLine = $env:PATCHY_THROTTLED_ARGS
if (-not $commandLine) {
  [Console]::Error.WriteLine('build-busy-check.ps1: run it through scripts\run-throttled.bat (PATCHY_THROTTLED_ARGS is empty)')
  exit 2
}

$mutex = $null
$held = $false
try {
  try {
    $mutex = New-Object System.Threading.Mutex($false, 'Global\PatchyLocalBuild')
    try { $held = $mutex.WaitOne(0) }
    catch [System.Threading.AbandonedMutexException] { $held = $true }  # its holder died: ours now
  }
  catch [System.UnauthorizedAccessException] {
    # An elevated holder's mutex refuses a non-elevated opener; the mutex only exists while a
    # holder has it open, so the lock is taken.
    $held = $false
  }
  if (-not $held) {
    $holders = @(Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe' OR Name = 'pwsh.exe'" |
      Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -match 'build-busy-check\.ps1' } |
      ForEach-Object { "PID $($_.ProcessId)" })
    $who = if ($holders.Count) { " (lock held by the wrapper $($holders -join ', '))" } else { '' }
    Write-Busy "another build holds this machine's build lock$who; running build processes:" (Get-RunningBuilds)
    exit 3
  }

  $outside = Get-RunningBuilds
  if ($outside.Count -gt 0) {
    Write-Busy 'another build is already running on this machine (started without the build lock):' $outside
    exit 3
  }

  # Run the batch file again with the original argument line, verbatim: ProcessStartInfo takes
  # the command line as one string, so PowerShell's native-argument quoting never touches it.
  # No redirection, so the build writes straight to this console or pipe.
  $env:PATCHY_THROTTLED_ARGS = $null
  $env:PATCHY_BUILD_LOCK_HELD = '1'
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = Join-Path $env:SystemRoot 'System32\cmd.exe'
  $psi.Arguments = '/d /s /c ""' + (Join-Path $PSScriptRoot 'run-throttled.bat') + '" ' + $commandLine + '"'
  $psi.UseShellExecute = $false
  $child = [System.Diagnostics.Process]::Start($psi)
  $child.WaitForExit()
  exit $child.ExitCode
}
finally {
  if ($held) { try { $mutex.ReleaseMutex() } catch { } }
  if ($mutex) { $mutex.Dispose() }
}

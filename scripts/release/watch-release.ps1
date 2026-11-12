<#
.SYNOPSIS
Waits on release targets for an unattended run and always comes back with news or a check-in.

.DESCRIPTION
Polls build\release-logs for the .exit markers that release-worker.bat writes and ends, with a
state table, as soon as there is something to act on:

  exit 0  a watched target finished with exit=0 (others may still be running)
  exit 2  a watched target finished with a nonzero code
  exit 3  no running target's logs grew for -StallMinutes (look at the host before anything else)
  exit 5  -CheckInMinutes passed with no news: a routine wake-up, relaunch the watch
  exit 6  a target launched here wrote no log within two minutes

It never waits for "all done": one finished target ends the watch, so every result is looked at
when it lands, and a quiet stretch never lasts longer than -CheckInMinutes. Run it as a
harness-tracked background command and start it again after each return until nothing is left.

  -Wait   "a,b"      targets whose markers are news
  -Launch "a,b"      targets to start first (through release-all-automated.bat)
  -Chain  "a>b,c+d>e" start b once a ended with exit=0; e once c and d both did. A chained
                     target is not news unless -Wait names it too.

Launches never pipe, capture or wait on release-all-automated.bat: the console it starts
inherits a pipe on the launching call, which then returns only when the whole target has
ended. A watch loop built on such a call sits frozen, its failure, stall and deadline checks
included (docs/release-process-scripts.md, "Waiting on targets").

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\watch-release.ps1 -Launch "windows,mac,linux,wasm" -Wait "windows,mac,linux,wasm" -Chain "windows>verify-windows,mac+linux>verify-mac,linux>verify-linux"
#>
param(
  [Parameter(Mandatory = $true)][string]$Wait,
  [string]$Launch = '',
  [string]$Chain = '',
  [double]$StallMinutes = 30,
  [double]$CheckInMinutes = 25,
  [int]$PollSeconds = 20,
  # For the self-test (watch-release.test.ps1): another marker folder and launcher.
  [string]$LogDir = '',
  [string]$Launcher = ''
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not (Test-Path -LiteralPath (Join-Path $repo 'CMakeLists.txt'))) { throw "not a Patchy checkout: $repo" }
if (-not $LogDir) { $LogDir = Join-Path $repo 'build\release-logs' }
if (-not $Launcher) { $Launcher = Join-Path $PSScriptRoot 'release-all-automated.bat' }
if (-not (Test-Path -LiteralPath $Launcher)) { throw "launcher not found: $Launcher" }
New-Item -ItemType Directory -Force $LogDir | Out-Null
# The batch files' inline powershell one-liners need the Windows PowerShell 5.1 module path
# when this runs under pwsh 7 (docs/release-process-scripts.md, "Agent/non-interactive runs").
$env:PSModulePath = "$env:USERPROFILE\Documents\WindowsPowerShell\Modules;$env:ProgramFiles\WindowsPowerShell\Modules;$env:SystemRoot\system32\WindowsPowerShell\v1.0\Modules"

function Split-Names([string]$text) { @($text -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }
function Get-Stamp { Get-Date -Format 'HH:mm:ss' }
function Get-Marker([string]$target) {
  $file = Join-Path $LogDir "$target.exit"
  if (Test-Path -LiteralPath $file) { return ([string](Get-Content -LiteralPath $file -Raw)).Trim() }
  return $null
}

# @(): a function's one-element array comes back as a bare string, and string + array joins text.
$waitFor = @(Split-Names $Wait)
if (-not $waitFor.Count) { throw '-Wait names no target' }
$chains = @()
foreach ($item in @(Split-Names $Chain)) {
  $sides = $item -split '>'
  if ($sides.Count -ne 2 -or -not $sides[0].Trim() -or -not $sides[1].Trim()) { throw "bad -Chain item '$item' (want a>b or a+b>c)" }
  $chains += , @(@($sides[0] -split '\+' | ForEach-Object { $_.Trim() } | Where-Object { $_ }), $sides[1].Trim())
}
$toLaunch = @(Split-Names $Launch)
$all = @($waitFor + $toLaunch + @($chains | ForEach-Object { $_[1] }) | Select-Object -Unique)

# verify-windows-arm64-suites.log belongs to verify-windows-arm64, not to verify-windows: a log
# goes to the longest target name it starts with.
$knownTargets = @('windows', 'mac', 'linux', 'wasm', 'wasm-local', 'verify-windows', 'verify-windows-arm64', 'verify-mac',
  'verify-linux', 'upload-all', 'upload-desktop', 'upload-wasm', 'upload-github', 'warncheck-windows',
  'warncheck-windows-arm64', 'warncheck-mac', 'warncheck-linux', 'warncheck-wasm', 'rehearse-windows',
  'rehearse-windows-arm64', 'rehearse-mac', 'rehearse-linux') + $all | Select-Object -Unique
$knownTargets = @($knownTargets)
function Get-LogOwner([string]$baseName) {
  $owner = $null
  foreach ($name in $knownTargets) {
    if (($baseName -eq $name -or $baseName.StartsWith("$name-")) -and ($null -eq $owner -or $name.Length -gt $owner.Length)) { $owner = $name }
  }
  return $owner
}
function Get-LogState([string]$target) {
  # A file another process holds open reports a stale size and time in a directory listing.
  $bytes = 0L; $last = [datetime]::MinValue
  foreach ($file in Get-ChildItem -LiteralPath $LogDir -Filter "$target*.log" -File -ErrorAction SilentlyContinue) {
    if ((Get-LogOwner $file.BaseName) -ne $target) { continue }
    try {
      $stream = [IO.File]::Open($file.FullName, 'Open', 'Read', 'ReadWrite,Delete')
      $bytes += $stream.Length; $stream.Dispose()
    } catch { $bytes += $file.Length }
    $file.Refresh(); if ($file.LastWriteTime -gt $last) { $last = $file.LastWriteTime }
  }
  return @{ Bytes = $bytes; Last = $last }
}
function Test-Started([string]$target) { Test-Path -LiteralPath (Join-Path $LogDir "$target.log") }
function Write-Table {
  foreach ($target in $all) {
    $marker = Get-Marker $target; $log = Get-LogState $target
    $state = if ($marker) { $marker } elseif (Test-Started $target) { 'running' } else { 'not started' }
    $seen = if ($log.Last -gt [datetime]::MinValue) { '{0:HH:mm:ss}' -f $log.Last } else { '-' }
    Write-Output ('  {0,-26} {1,-12} logs {2,10} bytes, last write {3}' -f $target, $state, $log.Bytes, $seen)
  }
}
$launched = @{}
function Start-Target([string]$target) {
  $before = Get-Date
  # ShellExecute, nothing redirected, no -Wait: the batch file starts its own console and returns.
  Start-Process -FilePath $env:ComSpec -ArgumentList '/c', "`"$Launcher`"", $target -WorkingDirectory $repo -WindowStyle Hidden
  $script:launched[$target] = Get-Date
  Write-Output ('{0} launched {1} (the launch call returned in {2:N1} s)' -f (Get-Stamp), $target, ((Get-Date) - $before).TotalSeconds)
}

foreach ($target in $toLaunch) {
  $stale = Join-Path $LogDir "$target.exit"
  if (Test-Path -LiteralPath $stale) { Remove-Item -LiteralPath $stale -Force }
  Start-Target $target
}
# What had already finished when the watch began is not news.
$known = @{}
foreach ($target in $waitFor) { $marker = Get-Marker $target; if ($marker -and -not $launched.ContainsKey($target)) { $known[$target] = $marker } }

$started = Get-Date
$lastGrowth = Get-Date
$lastBytes = -1L
while ($true) {
  # News first, so nothing below can hold a failure back.
  $news = @(); $bad = $false
  foreach ($target in $waitFor) {
    $marker = Get-Marker $target
    if ($marker -and $known[$target] -ne $marker) { $news += "$target ($marker)"; if ($marker -ne 'exit=0') { $bad = $true } }
  }
  # Chains fire on success only, before the watch ends, so the next step is already running.
  # Not $chain: variable names ignore case, and that is the [string] -Chain parameter.
  foreach ($link in $chains) {
    $next = $link[1]
    $ready = @($link[0] | Where-Object { (Get-Marker $_) -ne 'exit=0' }).Count -eq 0
    if ($ready -and -not $launched.ContainsKey($next) -and -not (Get-Marker $next) -and -not (Test-Started $next)) { Start-Target $next }
  }
  foreach ($target in @($launched.Keys)) {
    if (-not (Test-Started $target) -and -not (Get-Marker $target) -and ((Get-Date) - $launched[$target]).TotalMinutes -ge 2) {
      Write-Output ('{0} LAUNCH FAILED: {1} wrote no log within two minutes' -f (Get-Stamp), $target); Write-Table; exit 6
    }
  }
  if ($news.Count) {
    Write-Output ('{0} {1}: {2}' -f (Get-Stamp), $(if ($bad) { 'FAILED' } else { 'FINISHED' }), ($news -join ', '))
    Write-Table
    if ($bad) { exit 2 } else { exit 0 }
  }
  $running = @($all | Where-Object { -not (Get-Marker $_) -and (Test-Started $_) })
  $bytes = 0L; foreach ($target in $running) { $bytes += (Get-LogState $target).Bytes }
  if ($bytes -ne $lastBytes) { $lastBytes = $bytes; $lastGrowth = Get-Date }
  elseif ($running.Count -and ((Get-Date) - $lastGrowth).TotalMinutes -ge $StallMinutes) {
    Write-Output ('{0} STALL: no log of a running target grew for {1} min' -f (Get-Stamp), $StallMinutes); Write-Table; exit 3
  }
  if (((Get-Date) - $started).TotalMinutes -ge $CheckInMinutes) {
    Write-Output ('{0} CHECK-IN after {1} min with nothing finished' -f (Get-Stamp), $CheckInMinutes); Write-Table; exit 5
  }
  Start-Sleep -Seconds $PollSeconds
}

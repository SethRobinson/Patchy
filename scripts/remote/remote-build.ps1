<#
Builds the current working tree (uncommitted changes included) on a remote test machine
and runs the test suites there.

  scripts\remote\remote-build.ps1 -Target mac|linux|windows [-TestFilter ui_palette] [-CoreTestFilter psd]
                                  [-BuildTarget patchy_ui_visual_tests[,patchy_core_tests]] [-Preset opt-dev]
                                  [-SkipTests] [-FetchArtifacts]
  linux only:   [-NativeQt]
  windows only: [-WindowsHost <ssh target>] [-WaitMinutes 30] [-PerfScenario startup] [-PerfEnv 'NAME=value;...']

A typical worker loop builds only the suite it runs, in the optimized no-LTCG preset:

  scripts\remote\remote-build.ps1 -Target windows -WindowsHost <host> -Preset opt-dev `
    -BuildTarget patchy_ui_visual_tests -TestFilter ui_palette

-BuildTarget (comma-separated CMake targets) builds only those instead of the preset's
default target, and only the suites whose executables were built run (patchy_core_tests,
patchy_ui_visual_tests; on mac also patchy_mac_a11y_tests); a suite left out is reported as
not built rather than run from a stale binary. -Preset replaces the default preset
(windows `release`, linux `linux-flatpak-qt` or `linux-release` with -NativeQt, mac
`mac-release`). `opt-dev` (windows) is optimized without /GL and /LTCG, so a relink takes a
fraction of the release preset's; the release preset stays the handoff and packaging build.
The remote half reports the warnings the build printed; with -BuildTarget, another preset, or
an incremental build that count covers only what this run compiled and linked, so it is not
the zero-warning verification (docs/platform.md, "Warning verification").

Mechanism: a temporary-index git snapshot of the working tree is force-pushed to
refs/snapshots/dev on the remote's bare repo (~/patchy.git). No commit or branch is
created in this repository and the real index is untouched. The remote work tree
(~/patchy/src, created by setup-mac.sh / setup-linux.sh) checks the snapshot out
detached -- unchanged files keep their mtimes, so remote Ninja rebuilds stay
incremental -- then scripts/remote/build-and-test.sh configures, builds, and runs the
suites with output streamed back here. The remote exit code propagates.

-Target linux builds and tests inside the Flatpak SDK (preset linux-flatpak-qt), so the
suites run against the Qt that Linux users get. -NativeQt selects the older
linux-release preset (the aqt Qt in .deps) for the tools that need a host binary, such
as the linux-asan sync.

-Target windows offloads a Windows build to another Windows machine, only on explicit
request. Its work tree comes from the host config (setup-windows.ps1 creates it), the
remote half is build-and-test.ps1, and the preset is the real `release` preset. The
local build\release stays the release gate and the packaging source.

The windows host config may list several machines in order of preference. Each is
probed over ssh in that order: an unreachable one (switched off) is skipped, one that
is already compiling or testing is busy, and the first idle one builds. When every
reachable host is busy the run stops (exit 75) rather than start another build beside
them; -WaitMinutes N re-probes every minute for up to N minutes instead. -WindowsHost
picks one configured host by its ssh target; it is still probed, and a busy one stops the
run (exit 75). The mac and linux hosts are probed the same way (pgrep for compilers,
linkers, ninja, cmake and the suites) after the lock is taken: busy stops the run (exit 75).

Every target holds a per-host build lock (remote-lock.ps1) from before the snapshot push
until the artifacts are fetched: each host has one work tree and one snapshot ref, so a
second run on a locked host stops (exit 75) and names the holder instead of overwriting
the first run's snapshot mid-build. -BreakLock clears a leftover lock from a run that is
really gone. A lock left by a run from this machine is taken over automatically only when
that run's process is gone AND the host shows no build or test processes: a remote build
can outlive a killed ssh client.

The ssh target of each machine comes from scripts\remote\hosts.local.json (gitignored;
see remote-hosts.ps1 and hosts.example.json).

On a failing test run (or with -FetchArtifacts) the remote test-artifacts folder is
copied to build\remote-artifacts\<target>\ for inspection.
#>
param(
  [Parameter(Mandatory = $true)][ValidateSet('mac', 'linux', 'windows')][string]$Target,
  [string]$TestFilter = '',
  # Core-suite name filter.
  [string]$CoreTestFilter = '',
  # Build only these CMake targets (an array, or one comma-separated string), and run only the
  # suites among them. Default: the preset's default target and both suites.
  [string[]]$BuildTarget = @(),
  # Replace the default preset (windows release, mac mac-release, linux linux-flatpak-qt).
  [string]$Preset = '',
  [switch]$SkipTests,
  # windows only: run `patchy_perf_tests <scenario>` after the suites, with these NAME=value;...
  # variables (docs/startup-performance.md).
  [string]$PerfScenario = '',
  [string]$PerfEnv = '',
  [switch]$FetchArtifacts,
  # linux only: build against the aqt Qt in .deps instead of the Flatpak SDK's Qt.
  [switch]$NativeQt,
  # windows only: build on this configured host (its ssh target) instead of the first idle one.
  [string]$WindowsHost = '',
  # windows only: when every reachable host is busy, keep re-probing for this many minutes.
  [int]$WaitMinutes = 0,
  # Clear the target host's build lock first. Only after checking its holder is really gone.
  [switch]$BreakLock
)

# 'Continue', not 'Stop': under Windows PowerShell 5.1 any native stderr line
# (git notices, ssh/compiler chatter) becomes a terminating NativeCommandError
# with 'Stop'. Failures are handled via the explicit LASTEXITCODE checks below.
$ErrorActionPreference = 'Continue'

. "$PSScriptRoot\remote-hosts.ps1"
. "$PSScriptRoot\remote-lock.ps1"
if (($WindowsHost -or $WaitMinutes) -and $Target -ne 'windows') { throw '-WindowsHost and -WaitMinutes are only supported for -Target windows' }
if (($PerfScenario -or $PerfEnv) -and $Target -ne 'windows') { throw '-PerfScenario and -PerfEnv are only supported for -Target windows' }
$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) { throw 'remote-build.ps1 must run inside the Patchy repository' }
$lockOwner = Get-PatchyLockOwner $repoRoot.Trim()
$lockPath = $null
if ($NativeQt -and $Target -ne 'linux') { throw '-NativeQt is only supported for -Target linux' }
if ($NativeQt -and $Preset) { throw '-NativeQt and -Preset both pick the preset; pass only -Preset' }
# Names go into a remote command line: plain CMake names only.
$targets = @($BuildTarget | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($name in @($targets) + @($Preset | Where-Object { $_ })) {
  if ($name -notmatch '^[A-Za-z0-9_.+-]+$') { throw "'$name' is not a plain CMake target or preset name" }
}
if ($TestFilter -match "'" -or $CoreTestFilter -match "'") { throw 'test filters cannot contain single quotes' }

if ($Target -eq 'windows') {
  $candidates = Get-PatchyWindowsHosts
  if ($WindowsHost) {
    $remote = $candidates | Where-Object { $_.ssh -eq $WindowsHost } | Select-Object -First 1
    if (-not $remote) { throw "-WindowsHost '$WindowsHost' is not a windows entry in hosts.local.json ($(($candidates | ForEach-Object { $_.ssh }) -join ', '))" }
    $path = Get-PatchyLockPath $true $remote
    $why = Enter-PatchyRemoteLock $remote.ssh $true $path $lockOwner -BreakLock:$BreakLock
    if ($why) { Write-Host "== not starting: $why =="; exit 75 }
    $lockPath = $path
    # A run from a checkout without the lock (an older remote-build.ps1) shows only as processes.
    $state = Get-PatchyRemoteBuildState $remote.ssh $true
    if ($state -ne 'idle') {
      Exit-PatchyRemoteLock $remote.ssh $true $lockPath $lockOwner
      Write-Host "== not starting: windows host $($remote.ssh) is $state =="
      exit 75
    }
  }
  else {
    $remote = $null
    $deadline = (Get-Date).AddMinutes($WaitMinutes)
    while (-not $remote) {
      $anyReachable = $false
      foreach ($c in $candidates) {
        $state = Get-PatchyRemoteBuildState $c.ssh $true
        if ($state -eq 'idle') {
          $path = Get-PatchyLockPath $true $c
          $why = Enter-PatchyRemoteLock $c.ssh $true $path $lockOwner
          if ($why) { $state = "busy: $why" } else { $lockPath = $path }
        }
        Write-Host "== windows host $($c.ssh): $state =="
        if ($state -eq 'idle') { $remote = $c; break }
        if ($state -ne 'unreachable') { $anyReachable = $true }
      }
      if ($remote) { break }
      if (-not $anyReachable) { throw 'no windows build host is reachable (all switched off, or ssh is not set up)' }
      if ((Get-Date) -ge $deadline) {
        Write-Host 'every reachable windows build host is busy; not starting another build beside them (-WaitMinutes N waits)'
        exit 75
      }
      Start-Sleep -Seconds 60
    }
  }
}
else {
  $remote = Get-PatchyRemoteHost $Target
  $path = Get-PatchyLockPath $false $remote
  $why = Enter-PatchyRemoteLock $remote.ssh $false $path $lockOwner -BreakLock:$BreakLock
  if ($why) { Write-Host "== not starting: $why =="; exit 75 }
  $lockPath = $path
  # Another checkout's run (or a build started by hand in the shared ~/patchy/src) shows only
  # as processes.
  $state = Get-PatchyRemoteBuildState $remote.ssh $false
  if ($state -ne 'idle') {
    Exit-PatchyRemoteLock $remote.ssh $false $lockPath $lockOwner
    Write-Host "== not starting: $Target host $($remote.ssh) is $state =="
    exit 75
  }
}
$remoteHost = $remote.ssh
$preset = if ($Preset) { $Preset }
  elseif ($Target -eq 'windows') { 'release' }
  elseif ($Target -eq 'linux' -and -not $NativeQt) { 'linux-flatpak-qt' }
  else { "$Target-release" }

Push-Location $repoRoot.Trim()
try {
  # Snapshot the working tree with a temporary index; the real index stays untouched
  # (remote-snapshot.ps1).
  . "$PSScriptRoot\remote-snapshot.ps1"
  $snap = Push-PatchySnapshot -RemoteHost $remoteHost
  Write-Host "== building snapshot $($snap.Substring(0, 12)) on $remoteHost =="

  if ($Target -eq 'windows') {
    # A Windows OpenSSH shell is Windows PowerShell 5.1: no &&, and every step's exit code is
    # checked explicitly. Single quotes only: local PowerShell mangles embedded double
    # quotes in native-command arguments.
    $psArgs = @('-Preset', $preset)
    if ($TestFilter) { $psArgs += @('-Filter', "'$TestFilter'") }
    if ($CoreTestFilter) { $psArgs += @('-CoreFilter', "'$CoreTestFilter'") }
    if ($targets.Count) { $psArgs += @('-BuildTarget', "'$($targets -join ',')'") }
    if ($SkipTests) { $psArgs += '-SkipTests' }
    if ($PerfScenario) { $psArgs += @('-PerfScenario', "'$PerfScenario'") }
    if ($PerfEnv) { $psArgs += @('-PerfEnv', "'$PerfEnv'") }
    # Optional per-host parallelism from the host config; build-and-test.ps1 defaults to 8.
    if ($remote.jobs) { $psArgs += @('-Jobs', [int]$remote.jobs) }
    $wt = $remote.workTree.Replace('\', '/')
    $remoteCmd = "git -C $wt fetch -q origin +refs/snapshots/dev; " + 'if ($LASTEXITCODE) { exit $LASTEXITCODE }; ' +
      "git -C $wt checkout -q -f --detach FETCH_HEAD; " + 'if ($LASTEXITCODE) { exit $LASTEXITCODE }; ' +
      "powershell -NoProfile -ExecutionPolicy Bypass -File $wt/scripts/remote/build-and-test.ps1 " + ($psArgs -join ' ') + '; exit $LASTEXITCODE'
    $artifactSrc = "${remoteHost}:$wt/build/$preset/test-artifacts"
  }
  else {
    $shArgs = @($preset)
    if ($TestFilter) { $shArgs += @('--filter', "'$TestFilter'") }
    if ($CoreTestFilter) { $shArgs += @('--core-filter', "'$CoreTestFilter'") }
    if ($targets.Count) { $shArgs += @('--target', ($targets -join ',')) }
    if ($SkipTests) { $shArgs += '--skip-tests' }
    $remoteCmd = 'git -C ~/patchy/src fetch -q origin +refs/snapshots/dev && ' +
      'git -C ~/patchy/src checkout -q -f --detach FETCH_HEAD && ' +
      'bash ~/patchy/src/scripts/remote/build-and-test.sh ' + ($shArgs -join ' ')
    $artifactSrc = "${remoteHost}:patchy/src/build/$preset/test-artifacts"
  }
  ssh $remoteHost $remoteCmd
  $remoteExit = $LASTEXITCODE

  if ($FetchArtifacts -or ($remoteExit -ne 0 -and -not $SkipTests)) {
    $dest = Join-Path $repoRoot.Trim() "build\remote-artifacts\$Target"
    New-Item -ItemType Directory -Force $dest | Out-Null
    Write-Host "== fetching test-artifacts to $dest =="
    scp -q -r $artifactSrc $dest 2>$null
  }
  exit $remoteExit
}
finally {
  if ($lockPath) { Exit-PatchyRemoteLock $remoteHost ($Target -eq 'windows') $lockPath $lockOwner }
  Pop-Location
}

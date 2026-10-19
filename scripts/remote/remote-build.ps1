<#
Builds the current working tree (uncommitted changes included) on a remote test machine
and runs the test suites there.

  scripts\remote\remote-build.ps1 -Target mac [-TestFilter ui_palette] [-SkipTests] [-FetchArtifacts]
  scripts\remote\remote-build.ps1 -Target linux [-NativeQt] [-TestFilter ui_palette] [-SkipTests]
  scripts\remote\remote-build.ps1 -Target windows [-TestFilter ui_palette] [-CoreTestFilter psd] [-SkipTests]
                                  [-WindowsHost <ssh target>] [-WaitMinutes 30]

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
run (exit 75).

Every target holds a per-host build lock (remote-lock.ps1) from before the snapshot push
until the artifacts are fetched: each host has one work tree and one snapshot ref, so a
second run on a locked host stops (exit 75) and names the holder instead of overwriting
the first run's snapshot mid-build. -BreakLock clears a leftover lock from a run that is
really gone (a stale lock from this machine is taken over automatically).

The ssh target of each machine comes from scripts\remote\hosts.local.json (gitignored;
see remote-hosts.ps1 and hosts.example.json).

On a failing test run (or with -FetchArtifacts) the remote test-artifacts folder is
copied to build\remote-artifacts\<target>\ for inspection.
#>
param(
  [Parameter(Mandatory = $true)][ValidateSet('mac', 'linux', 'windows')][string]$Target,
  [string]$TestFilter = '',
  # windows only: core-suite name filter. mac/linux always run the full core suite.
  [string]$CoreTestFilter = '',
  [switch]$SkipTests,
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
$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) { throw 'remote-build.ps1 must run inside the Patchy repository' }
$lockOwner = Get-PatchyLockOwner $repoRoot.Trim()
$lockPath = $null

# Probes one windows host. Returns 'idle', 'busy: <process names>', or 'unreachable'.
# The remote shell is Windows PowerShell 5.1 (setup-windows-ssh.ps1 sets it). Get-Process
# with absent names sets a failing exit code, so the marker line, not the exit code, says
# whether the probe ran. Single quotes only: no local expansion, no embedded double quotes.
function Get-WindowsHostState([string]$sshTarget) {
  $probe = 'Get-Process cl,link,ninja,cmake,patchy_core_tests,patchy_ui_visual_tests -ErrorAction SilentlyContinue | ForEach-Object { $_.Name } | Sort-Object -Unique; ''patchy-probe-ok'''
  $out = @(ssh -o ConnectTimeout=8 -o BatchMode=yes $sshTarget $probe 2>$null)
  if ($out -notcontains 'patchy-probe-ok') { return 'unreachable' }
  $busy = @($out | Where-Object { $_ -and $_ -ne 'patchy-probe-ok' })
  if ($busy.Count -gt 0) { return "busy: $($busy -join ', ')" }
  return 'idle'
}

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
    $state = Get-WindowsHostState $remote.ssh
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
        $state = Get-WindowsHostState $c.ssh
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
}
$remoteHost = $remote.ssh
$preset = if ($Target -eq 'windows') { 'release' }
  elseif ($Target -eq 'linux' -and -not $NativeQt) { 'linux-flatpak-qt' }
  else { "$Target-release" }

Push-Location $repoRoot.Trim()
try {
  if ($NativeQt -and $Target -ne 'linux') { throw '-NativeQt is only supported for -Target linux' }
  if ($CoreTestFilter -and $Target -ne 'windows') { throw '-CoreTestFilter is only supported for -Target windows' }
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
    if ($SkipTests) { $psArgs += '-SkipTests' }
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
    if ($TestFilter) { $shArgs += @('--filter', $TestFilter) }
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

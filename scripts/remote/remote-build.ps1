<#
Builds the current working tree (uncommitted changes included) on a remote test machine
and runs the test suites there.

  scripts\remote\remote-build.ps1 -Target mac [-TestFilter ui_palette] [-SkipTests] [-FetchArtifacts]
  scripts\remote\remote-build.ps1 -Target windows [-TestFilter ui_palette] [-CoreTestFilter psd] [-SkipTests]

Mechanism: a temporary-index git snapshot of the working tree is force-pushed to
refs/snapshots/dev on the remote's bare repo (~/patchy.git). No commit or branch is
created in this repository and the real index is untouched. The remote work tree
(~/patchy/src, created by setup-mac.sh / setup-linux.sh) checks the snapshot out
detached -- unchanged files keep their mtimes, so remote Ninja rebuilds stay
incremental -- then scripts/remote/build-and-test.sh configures, builds, and runs the
suites with output streamed back here. The remote exit code propagates.

-Target windows offloads a Windows build to another Windows machine, only on explicit
request. Its work tree comes from the host config (setup-windows.ps1 creates it), the
remote half is build-and-test.ps1, and the preset is the real `release` preset. The
local build\release stays the release gate and the packaging source.

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
  [switch]$FetchArtifacts
)

# 'Continue', not 'Stop': under Windows PowerShell 5.1 any native stderr line
# (git notices, ssh/compiler chatter) becomes a terminating NativeCommandError
# with 'Stop'. Failures are handled via the explicit LASTEXITCODE checks below.
$ErrorActionPreference = 'Continue'

. "$PSScriptRoot\remote-hosts.ps1"
$remote = Get-PatchyRemoteHost $Target
$remoteHost = $remote.ssh
$preset = if ($Target -eq 'windows') { 'release' } else { "$Target-release" }
if ($CoreTestFilter -and $Target -ne 'windows') { throw '-CoreTestFilter is only supported for -Target windows' }

$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) { throw 'remote-build.ps1 must run inside the Patchy repository' }
Push-Location $repoRoot.Trim()
try {
  # Snapshot the working tree with a temporary index; the real index stays untouched
  # (remote-snapshot.ps1, shared with the wasm-release-st offload).
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
  Pop-Location
}

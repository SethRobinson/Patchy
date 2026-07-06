<#
Builds the current working tree (uncommitted changes included) on a remote test machine
and runs the test suites there.

  scripts\remote\remote-build.ps1 -Target mac [-TestFilter ui_palette] [-SkipTests] [-FetchArtifacts]

Mechanism: a temporary-index git snapshot of the working tree is force-pushed to
refs/snapshots/dev on the remote's bare repo (~/patchy.git). No commit or branch is
created in this repository and the real index is untouched. The remote work tree
(~/patchy/src, created by setup-mac.sh / setup-linux.sh) checks the snapshot out
detached -- unchanged files keep their mtimes, so remote Ninja rebuilds stay
incremental -- then scripts/remote/build-and-test.sh configures, builds, and runs the
suites with output streamed back here. The remote exit code propagates.

On a failing test run (or with -FetchArtifacts) the remote test-artifacts folder is
copied to build\remote-artifacts\<target>\ for inspection.
#>
param(
  [Parameter(Mandatory = $true)][ValidateSet('mac', 'linux')][string]$Target,
  [string]$TestFilter = '',
  [switch]$SkipTests,
  [switch]$FetchArtifacts
)

# 'Continue', not 'Stop': under Windows PowerShell 5.1 any native stderr line
# (git notices, ssh/compiler chatter) becomes a terminating NativeCommandError
# with 'Stop'. Failures are handled via the explicit LASTEXITCODE checks below.
$ErrorActionPreference = 'Continue'

$remoteHost = if ($Target -eq 'mac') { 'seth@studiomac.local' } else { 'glados@glados.local' }
$preset = "$Target-release"

$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) { throw 'remote-build.ps1 must run inside the Patchy repository' }
Push-Location $repoRoot.Trim()
try {
  # Snapshot the working tree with a temporary index; the real index stays untouched.
  # PID-suffixed so concurrent invocations (e.g. -Target mac and -Target linux at once)
  # don't clobber each other's snapshot.
  $gitDir = (git rev-parse --absolute-git-dir).Trim()
  $tmpIndex = Join-Path $gitDir "patchy-remote-$PID.index"
  $env:GIT_INDEX_FILE = $tmpIndex
  try {
    git read-tree HEAD
    if ($LASTEXITCODE -ne 0) { throw 'git read-tree failed' }
    # Filter the per-file autocrlf normalization notices (pre-existing working-copy line
    # endings vs .gitattributes); anything else on stderr still shows.
    git add -A 2>&1 | Where-Object { $_ -notmatch 'will be replaced by (LF|CRLF)' } | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw 'git add failed' }
    $tree = (git write-tree).Trim()
    $snap = (git commit-tree $tree -p HEAD -m 'patchy remote build snapshot').Trim()
    if (-not $snap) { throw 'git commit-tree failed' }
  }
  finally {
    Remove-Item Env:GIT_INDEX_FILE -ErrorAction SilentlyContinue
    if (Test-Path $tmpIndex) { Remove-Item $tmpIndex -Force -ErrorAction SilentlyContinue }
  }

  Write-Host "== pushing snapshot $($snap.Substring(0, 12)) to $remoteHost =="
  git push --force --quiet "${remoteHost}:patchy.git" "${snap}:refs/snapshots/dev"
  if ($LASTEXITCODE -ne 0) { throw 'git push to the remote bare repo failed (run the setup script first?)' }

  $shArgs = @($preset)
  if ($TestFilter) { $shArgs += @('--filter', $TestFilter) }
  if ($SkipTests) { $shArgs += '--skip-tests' }
  $remoteCmd = 'git -C ~/patchy/src fetch -q origin +refs/snapshots/dev && ' +
    'git -C ~/patchy/src checkout -q -f --detach FETCH_HEAD && ' +
    'bash ~/patchy/src/scripts/remote/build-and-test.sh ' + ($shArgs -join ' ')
  ssh $remoteHost $remoteCmd
  $remoteExit = $LASTEXITCODE

  if ($FetchArtifacts -or ($remoteExit -ne 0 -and -not $SkipTests)) {
    $dest = Join-Path $repoRoot.Trim() "build\remote-artifacts\$Target"
    New-Item -ItemType Directory -Force $dest | Out-Null
    Write-Host "== fetching test-artifacts to $dest =="
    scp -q -r "${remoteHost}:patchy/src/build/$preset/test-artifacts" $dest 2>$null
  }
  exit $remoteExit
}
finally {
  Pop-Location
}

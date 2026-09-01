<#
Builds the distributable macOS dmg from the current working tree and copies it into
build\package\ next to the Windows artifacts.

  scripts\remote\release-mac.ps1

Flow: snapshot + remote mac-release build (scripts\remote\remote-build.ps1 -SkipTests),
then packaging/macos/make-dmg.sh on studiomac (macdeployqt -> codesign -> dmg ->
notarize -> staple -> verify), then scp the dmg back.

PATCHY_REQUIRE_SIGNING=1 is passed so make-dmg.sh treats a missing signing identity or
notary profile as a hard error instead of quietly producing an unsigned dmg, and proves
the finished artifact with stapler validate + spctl. This script builds what gets
published, so an unsigned result is a failure, not a fallback (see
packaging/macos/README.md). It also runs -SkipTests: run the full suites separately
(scripts\remote\remote-build.ps1 -Target mac) before publishing, as AGENTS.md requires.
#>
param()
# 'Continue', not 'Stop': under Windows PowerShell 5.1 any native stderr line
# (git notices, ssh/compiler chatter) becomes a terminating NativeCommandError
# with 'Stop'. Failures are handled via the explicit LASTEXITCODE checks below.
$ErrorActionPreference = 'Continue'

$remoteHost = 'seth@studiomac.local'

# Delete previous local copies up front so a failed run leaves nothing stale for the
# newest-file upload script to pick up by accident (the remote side does the same).
$repoRoot = (git rev-parse --show-toplevel).Trim()
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*.dmg') -Force -ErrorAction SilentlyContinue

& "$PSScriptRoot\remote-build.ps1" -Target mac -SkipTests
if ($LASTEXITCODE -ne 0) { throw 'remote mac build failed' }

ssh $remoteHost 'source ~/.patchy-release-env 2>/dev/null || true; PATCHY_REQUIRE_SIGNING=1 bash ~/patchy/src/packaging/macos/make-dmg.sh'
if ($LASTEXITCODE -ne 0) {
  throw 'make-dmg.sh failed on studiomac (signing, notarization, and the Gatekeeper check are fatal here)'
}

$repoRoot = (git rev-parse --show-toplevel).Trim()
$dest = Join-Path $repoRoot 'build\package'
New-Item -ItemType Directory -Force $dest | Out-Null
scp -q "${remoteHost}:patchy/src/build/package/Patchy-*.dmg" $dest
if ($LASTEXITCODE -ne 0) { throw 'copying the dmg back from studiomac failed' }

# upload-mac-to-rtsoft.bat picks the newest build\package\Patchy-*.dmg, so an empty
# copy-back would leave it with nothing to publish (or something stale from an earlier
# version, had the delete-previous step above not run).
$dmgs = @(Get-ChildItem $dest -Filter 'Patchy-*.dmg')
if ($dmgs.Count -eq 0) { throw "scp reported success but no Patchy-*.dmg landed in $dest" }
Write-Host "== dmg copied into $dest =="
$dmgs | ForEach-Object { Write-Host ("{0}  ({1:N0} bytes)" -f $_.FullName, $_.Length) }

<#
Builds the distributable macOS dmg from the current working tree and copies it into
build\package\ next to the Windows artifacts.

  scripts\remote\release-mac.ps1

Flow: snapshot + remote mac-release build (scripts\remote\remote-build.ps1 -SkipTests),
then packaging/macos/make-dmg.sh on studiomac (macdeployqt -> codesign -> dmg ->
notarize; signing runs only if ~/.patchy-release-env provides the identities — see
packaging/macos/README.md), then scp the dmg back.
#>
param()
$ErrorActionPreference = 'Stop'

$remoteHost = 'seth@studiomac.local'
& "$PSScriptRoot\remote-build.ps1" -Target mac -SkipTests
if ($LASTEXITCODE -ne 0) { throw 'remote mac build failed' }

ssh $remoteHost 'source ~/.patchy-release-env 2>/dev/null || true; bash ~/patchy/src/packaging/macos/make-dmg.sh'
if ($LASTEXITCODE -ne 0) { throw 'make-dmg.sh failed on studiomac' }

$repoRoot = (git rev-parse --show-toplevel).Trim()
$dest = Join-Path $repoRoot 'build\package'
New-Item -ItemType Directory -Force $dest | Out-Null
scp -q "${remoteHost}:patchy/src/build/package/Patchy-*.dmg" $dest
Write-Host "== dmg copied into $dest =="
Get-ChildItem $dest -Filter 'Patchy-*.dmg' | ForEach-Object { Write-Host $_.FullName }

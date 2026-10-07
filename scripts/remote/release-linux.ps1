<#
Builds the distributable Linux Flatpak bundle and the signed Flatpak repository from the
current working tree and copies both into build\package\ next to the Windows artifacts.

  scripts\remote\release-linux.ps1

Flow: snapshot push (scripts\remote\remote-build.ps1 -SkipTests validates the tree
still builds against the aqt Qt), then packaging/linux/make-flatpak.sh on the linux build host
(flatpak-builder against the org.kde.Platform runtime -> signed repository -> flatpak
build-bundle), then scp the bundle and the repository tar back. One-time build-host setup
is in the make-flatpak.sh header (flatpak/flatpak-builder via apt) and in
packaging/linux/README.md (the repository signing key, named in ~/.patchy-release-env).

PATCHY_REQUIRE_FLATPAK_REPO=1 is passed so make-flatpak.sh treats a missing signing key as
a hard error: a release bundle built without the repository would install fine and then
never update.
#>
param()
# 'Continue', not 'Stop': under Windows PowerShell 5.1 any native stderr line
# (git notices, ssh/compiler chatter) becomes a terminating NativeCommandError
# with 'Stop'. Failures are handled via the explicit LASTEXITCODE checks below.
$ErrorActionPreference = 'Continue'

. (Join-Path $PSScriptRoot 'remote-hosts.ps1')
$remoteHost = (Get-PatchyRemoteHost linux).ssh

# Delete previous local copies up front so a failed run leaves nothing stale for the
# newest-file upload script to pick up by accident (the remote side does the same).
$repoRoot = (git rev-parse --show-toplevel).Trim()
if (-not $repoRoot -or -not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) { throw 'could not find the repository root' }
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*.flatpak') -Force -ErrorAction SilentlyContinue
# The upload script's staging copy of the PREVIOUS version must go too: it carries the
# final published name, so a stale one sitting beside a fresh versioned bundle reads as the
# release being ready when it is not (Seth, September 2026).
Remove-Item (Join-Path $repoRoot 'build\package\PatchyLinux.flatpak') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*-flatpak-repo.tar') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\PatchyFlatpakRepo.tar') -Force -ErrorAction SilentlyContinue

& "$PSScriptRoot\remote-build.ps1" -Target linux -SkipTests
if ($LASTEXITCODE -ne 0) { throw 'remote linux build failed' }

ssh $remoteHost 'source ~/.patchy-release-env 2>/dev/null || true; PATCHY_REQUIRE_FLATPAK_REPO=1 bash ~/patchy/src/packaging/linux/make-flatpak.sh'
if ($LASTEXITCODE -ne 0) { throw 'make-flatpak.sh failed on the linux build host' }

$repoRoot = (git rev-parse --show-toplevel).Trim()
if (-not $repoRoot -or -not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) { throw 'could not find the repository root' }
$dest = Join-Path $repoRoot 'build\package'
New-Item -ItemType Directory -Force $dest | Out-Null
scp -q "${remoteHost}:patchy/src/build/package/Patchy-*.flatpak" "${remoteHost}:patchy/src/build/package/Patchy-*-flatpak-repo.tar" $dest
if ($LASTEXITCODE -ne 0) { throw 'could not copy the flatpak bundle and repository tar from the linux build host' }
Write-Host "== flatpak bundle and repository copied into $dest =="
Get-ChildItem $dest | Where-Object { $_.Name -like 'Patchy-*.flatpak' -or $_.Name -like 'Patchy-*-flatpak-repo.tar' } |
  ForEach-Object { Write-Host $_.FullName }

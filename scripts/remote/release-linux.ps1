<#
Builds the distributable Linux Flatpak bundle, the signed Flatpak repository and the
AppImage (docs/appimage.md; packaging/linux/make-appimage.sh) from the current working tree
and copies them into build\package\ next to the Windows artifacts.

  scripts\remote\release-linux.ps1

Flow: snapshot push (scripts\remote\remote-build.ps1 -SkipTests validates the tree
still builds, tests included, inside the Flatpak SDK), then packaging/linux/make-flatpak.sh on the linux build host
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

# Both Linux packages install packaging/linux/com.rtsoft.patchy.metainfo.xml, whose
# screenshot URLs are pinned to a release tag. Refuse to package while that pin is behind
# docs/images/screenshots or the <release> entry is missing (scripts/release/check-metainfo.py;
# 1.17 and 1.18 shipped v1.16's pictures that way). Before the deletes: a refused run leaves
# the previous artifacts alone.
$python = Get-Command python -CommandType Application -ErrorAction SilentlyContinue
if (-not $python) { throw 'python is not on PATH (scripts\release\check-metainfo.py needs Python 3)' }
& $python.Source (Join-Path $PSScriptRoot '..\release\check-metainfo.py')
if ($LASTEXITCODE -ne 0) {
  throw 'packaging/linux/com.rtsoft.patchy.metainfo.xml does not describe this release (errors above; python scripts\release\check-metainfo.py --fix)'
}

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
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*.AppImage') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*.AppImage.zsync') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\PatchyLinux.AppImage') -Force -ErrorAction SilentlyContinue

& "$PSScriptRoot\remote-build.ps1" -Target linux -SkipTests
if ($LASTEXITCODE -ne 0) { throw 'remote linux build failed' }

ssh $remoteHost 'source ~/.patchy-release-env 2>/dev/null || true; PATCHY_REQUIRE_FLATPAK_REPO=1 bash ~/patchy/src/packaging/linux/make-flatpak.sh'
if ($LASTEXITCODE -ne 0) { throw 'make-flatpak.sh failed on the linux build host' }

# The AppImage (docs/appimage.md): built in an Ubuntu 22.04 podman container from the
# same snapshot, smoke-checked inside it and again on the host through FUSE.
ssh $remoteHost 'bash ~/patchy/src/packaging/linux/make-appimage.sh'
if ($LASTEXITCODE -ne 0) { throw 'make-appimage.sh failed on the linux build host' }

$repoRoot = (git rev-parse --show-toplevel).Trim()
if (-not $repoRoot -or -not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) { throw 'could not find the repository root' }
$dest = Join-Path $repoRoot 'build\package'
New-Item -ItemType Directory -Force $dest | Out-Null
scp -q "${remoteHost}:patchy/src/build/package/Patchy-*.flatpak" "${remoteHost}:patchy/src/build/package/Patchy-*-flatpak-repo.tar" `
  "${remoteHost}:patchy/src/build/package/Patchy-*.AppImage" "${remoteHost}:patchy/src/build/package/Patchy-*.AppImage.zsync" $dest
if ($LASTEXITCODE -ne 0) { throw 'could not copy the flatpak bundle, repository tar and AppImage from the linux build host' }
Write-Host "== flatpak bundle, repository and AppImage copied into $dest =="
Get-ChildItem $dest | Where-Object { $_.Name -like 'Patchy-*.flatpak' -or $_.Name -like 'Patchy-*-flatpak-repo.tar' -or $_.Name -like 'Patchy-*.AppImage*' } |
  ForEach-Object { Write-Host $_.FullName }

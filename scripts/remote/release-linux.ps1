<#
Builds the distributable Linux packages from the current working tree and copies them into
build\package\ next to the Windows artifacts: the x86_64 and aarch64 Flatpak bundles, the
signed Flatpak repository that serves both arches, and the x86_64 AppImage (docs/appimage.md;
packaging/linux/make-appimage.sh).

  scripts\remote\release-linux.ps1

Flow (packaging/linux/README.md):
1. In the background, the linux-arm64 host (a Lima VM on the mac build host, hosts.local.json)
   builds an unsigned aarch64 bundle with packaging/linux/make-flatpak.sh
   (scripts/remote/linux-arm64-vm.sh build), which also runs its sandboxed headless smoke check.
2. Meanwhile, a snapshot push (scripts\remote\remote-build.ps1 -SkipTests validates the tree
   still builds, tests included, inside the Flatpak SDK) to the linux build host.
3. The aarch64 bundle is copied to the linux build host, whose make-flatpak.sh builds x86_64,
   imports aarch64, signs both into one repository (one flatpakref for every arch) and
   exports a signed bundle per arch.
4. The AppImage (x86_64 only), then scp of the bundles, the repository tar and the AppImage.
5. The signed aarch64 bundle is installed in the VM and smoke-checked
   (scripts/remote/linux-arm64-vm.sh verify).
6. PatchyLinuxScannerSources.zip: the source of the sane-backends and libgphoto2 the Flatpak
   carries, which their licenses require beside the bundles
   (scripts/release/make-linux-scanner-sources.ps1; docs/legal-licensing.md).

One-time build-host setup: the make-flatpak.sh header (flatpak/flatpak-builder via apt),
packaging/linux/README.md (the repository signing key, named in ~/.patchy-release-env, and
the aarch64 VM).

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
$armEntry = Get-PatchyRemoteHost linux-arm64

# Both Linux packages install packaging/linux/com.rtsoft.patchy.metainfo.xml, whose
# screenshot URLs are pinned to a release tag. Refuse to package while that pin is behind
# docs/images/screenshots or the <release> entry is missing (scripts/release/check-metainfo.py;
# 1.17 and 1.18 shipped v1.16's pictures that way). Before the deletes: a refused run leaves
# the previous artifacts alone.
# The first python on PATH, and an exit code that fails unless the check really ran
# (publish-github-release.ps1 says why).
$python = Get-Command python -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $python) { throw 'python is not on PATH (scripts\release\check-metainfo.py needs Python 3)' }
$global:LASTEXITCODE = 1
& $python.Source (Join-Path $PSScriptRoot '..\release\check-metainfo.py')
if ($LASTEXITCODE -ne 0) {
  throw 'packaging/linux/com.rtsoft.patchy.metainfo.xml does not describe this release (errors above; python scripts\release\check-metainfo.py --fix)'
}

# Delete previous local copies up front so a failed run leaves nothing stale for the
# newest-file upload script to pick up by accident (the remote side does the same).
$repoRoot = (git rev-parse --show-toplevel).Trim()
if (-not $repoRoot -or -not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) { throw 'could not find the repository root' }
$cmake = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt')
$versionMatch = [regex]::Match($cmake, 'project\s*\([\s\S]*?\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})', 'IgnoreCase')
if (-not $versionMatch.Success) { throw 'could not read the project VERSION from CMakeLists.txt' }
$version = $versionMatch.Groups[1].Value
# Patchy-*.flatpak covers the x86_64 and the -aarch64 bundles.
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*.flatpak') -Force -ErrorAction SilentlyContinue
# The upload script's staging copy of the PREVIOUS version must go too: it carries the
# final published name, so a stale one sitting beside a fresh versioned bundle reads as the
# release being ready when it is not (Seth, September 2026).
Remove-Item (Join-Path $repoRoot 'build\package\PatchyLinux.flatpak') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\PatchyLinuxArm64.flatpak') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*-flatpak-repo.tar') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\PatchyFlatpakRepo.tar') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*.AppImage') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*.AppImage.zsync') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\PatchyLinux.AppImage') -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repoRoot 'build\package\PatchyLinuxScannerSources.zip') -Force -ErrorAction SilentlyContinue
$importDir = Join-Path $repoRoot 'build\flatpak-import'
New-Item -ItemType Directory -Force $importDir | Out-Null
Remove-Item (Join-Path $importDir 'Patchy-*-aarch64.flatpak') -Force -ErrorAction SilentlyContinue

# 1. The aarch64 build, in the background. The job pushes its own snapshot to the mac host's
# bare repo (a private ref; the host's ~/patchy/src and build lock are never touched) and
# runs this snapshot's linux-arm64-vm.sh there.
$armOutDir = '~/.patchy-tools/linux-arm64-out'
$armBundleName = "Patchy-$version-aarch64.flatpak"
$armJob = Start-Job -ArgumentList $repoRoot, $PSScriptRoot, $armEntry.ssh, $armEntry.lima, $armEntry.limactl, $version, $armOutDir, $importDir, $armBundleName -ScriptBlock {
  param($repoRoot, $scriptRoot, $armSsh, $vm, $limactl, $version, $outDir, $importDir, $bundleName)
  $ErrorActionPreference = 'Continue'
  Set-Location $repoRoot
  . (Join-Path $scriptRoot 'remote-snapshot.ps1')
  try {
    $sha = Push-PatchySnapshot -RemoteHost $armSsh -Ref 'refs/snapshots/linux-arm64'
  } catch {
    Write-Output "ERROR: $_"
    return 1
  }
  $script = '~/.patchy-tools/linux-arm64-vm.sh'
  ssh -n $armSsh "mkdir -p ~/.patchy-tools && git --git-dir=`$HOME/patchy.git show ${sha}:scripts/remote/linux-arm64-vm.sh > $script && bash $script build '$limactl' '$vm' $sha $version $outDir" 2>&1 |
    ForEach-Object { "$_" }
  if ($LASTEXITCODE -ne 0) { Write-Output 'ERROR: the aarch64 build failed in the VM'; return 1 }
  scp -q "${armSsh}:$($outDir.Replace('~/', ''))/$bundleName" $importDir 2>&1 | ForEach-Object { "$_" }
  if ($LASTEXITCODE -ne 0) { Write-Output 'ERROR: could not copy the aarch64 bundle from the mac host'; return 1 }
  return 0
}
Write-Host "== aarch64 Flatpak build started on $($armEntry.ssh) (VM $($armEntry.lima)) =="

# 2. The x86_64 tree check on the linux build host.
& "$PSScriptRoot\remote-build.ps1" -Target linux -SkipTests
$x64BuildExit = $LASTEXITCODE

Write-Host '== waiting for the aarch64 Flatpak build =='
$armResult = @(Receive-Job -Job $armJob -Wait -AutoRemoveJob)
$armResult | Select-Object -SkipLast 1 | ForEach-Object { Write-Host $_ }
$armExit = $armResult[-1]
if ($x64BuildExit -ne 0) { throw 'remote linux build failed' }
if ($armExit -ne 0) { throw 'the aarch64 Flatpak build failed (output above)' }
$armUnsigned = Join-Path $importDir $armBundleName
if (-not (Test-Path -LiteralPath $armUnsigned)) { throw "the aarch64 bundle is missing: $armUnsigned" }

# 3. Sign both arches into the repository on the linux build host.
ssh -n $remoteHost 'mkdir -p ~/patchy/src/build/flatpak-import && rm -f ~/patchy/src/build/flatpak-import/Patchy-*-aarch64.flatpak'
if ($LASTEXITCODE -ne 0) { throw 'could not prepare build/flatpak-import on the linux build host' }
scp -q $armUnsigned "${remoteHost}:patchy/src/build/flatpak-import/"
if ($LASTEXITCODE -ne 0) { throw 'could not copy the aarch64 bundle to the linux build host' }
ssh -n $remoteHost "source ~/.patchy-release-env 2>/dev/null || true; PATCHY_REQUIRE_FLATPAK_REPO=1 PATCHY_FLATPAK_IMPORT_BUNDLES=aarch64=`$HOME/patchy/src/build/flatpak-import/$armBundleName bash ~/patchy/src/packaging/linux/make-flatpak.sh"
if ($LASTEXITCODE -ne 0) { throw 'make-flatpak.sh failed on the linux build host' }

# 4. The AppImage (docs/appimage.md): built in an Ubuntu 22.04 podman container from the
# same snapshot, smoke-checked inside it and again on the host through FUSE.
ssh -n $remoteHost 'bash ~/patchy/src/packaging/linux/make-appimage.sh'
if ($LASTEXITCODE -ne 0) { throw 'make-appimage.sh failed on the linux build host' }

$dest = Join-Path $repoRoot 'build\package'
New-Item -ItemType Directory -Force $dest | Out-Null
scp -q "${remoteHost}:patchy/src/build/package/Patchy-*.flatpak" "${remoteHost}:patchy/src/build/package/Patchy-*-flatpak-repo.tar" `
  "${remoteHost}:patchy/src/build/package/Patchy-*.AppImage" "${remoteHost}:patchy/src/build/package/Patchy-*.AppImage.zsync" $dest
if ($LASTEXITCODE -ne 0) { throw 'could not copy the flatpak bundles, repository tar and AppImage from the linux build host' }
foreach ($required in @("Patchy-$version.flatpak", $armBundleName, "Patchy-$version-flatpak-repo.tar")) {
  if (-not (Test-Path -LiteralPath (Join-Path $dest $required))) { throw "build\package is missing $required after the copy" }
}

# 5. The signed aarch64 bundle, as users get it, installed and smoke-checked in the VM.
$armSsh = $armEntry.ssh
scp -q (Join-Path $dest $armBundleName) "${armSsh}:.patchy-tools/linux-arm64-out/signed-$armBundleName"
if ($LASTEXITCODE -ne 0) { throw 'could not copy the signed aarch64 bundle to the mac host' }
ssh -n $armSsh "bash ~/.patchy-tools/linux-arm64-vm.sh verify '$($armEntry.limactl)' '$($armEntry.lima)' ~/.patchy-tools/linux-arm64-out/signed-$armBundleName"
if ($LASTEXITCODE -ne 0) { throw 'the signed aarch64 bundle failed its checks in the VM' }

# 6. The source of the scanner and camera libraries the Flatpak carries, from the same manifest.
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'scripts\release\make-linux-scanner-sources.ps1')
if ($LASTEXITCODE -ne 0) { throw 'make-linux-scanner-sources.ps1 failed (output above)' }

Write-Host "== flatpak bundles, repository, AppImage and scanner sources in $dest =="
Get-ChildItem $dest | Where-Object { $_.Name -like 'Patchy-*.flatpak' -or $_.Name -like 'Patchy-*-flatpak-repo.tar' -or $_.Name -like 'Patchy-*.AppImage*' -or $_.Name -eq 'PatchyLinuxScannerSources.zip' } |
  ForEach-Object { Write-Host $_.FullName }

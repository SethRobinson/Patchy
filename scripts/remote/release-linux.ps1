<#
Builds the distributable Linux Flatpak bundle from the current working tree and copies
it into build\package\ next to the Windows artifacts.

  scripts\remote\release-linux.ps1

Flow: snapshot push (scripts\remote\remote-build.ps1 -SkipTests validates the tree
still builds against the aqt Qt), then packaging/linux/make-flatpak.sh on glados
(flatpak-builder against org.kde.Platform//6.8 -> flatpak build-bundle), then scp the
bundle back. One-time glados setup is in the make-flatpak.sh header (needs
flatpak/flatpak-builder via apt).
#>
param()
# 'Continue', not 'Stop': under Windows PowerShell 5.1 any native stderr line
# (git notices, ssh/compiler chatter) becomes a terminating NativeCommandError
# with 'Stop'. Failures are handled via the explicit LASTEXITCODE checks below.
$ErrorActionPreference = 'Continue'

$remoteHost = 'glados@glados.local'

# Delete previous local copies up front so a failed run leaves nothing stale for the
# newest-file upload script to pick up by accident (the remote side does the same).
$repoRoot = (git rev-parse --show-toplevel).Trim()
Remove-Item (Join-Path $repoRoot 'build\package\Patchy-*.flatpak') -Force -ErrorAction SilentlyContinue

& "$PSScriptRoot\remote-build.ps1" -Target linux -SkipTests
if ($LASTEXITCODE -ne 0) { throw 'remote linux build failed' }

ssh $remoteHost 'bash ~/patchy/src/packaging/linux/make-flatpak.sh'
if ($LASTEXITCODE -ne 0) { throw 'make-flatpak.sh failed on glados' }

$repoRoot = (git rev-parse --show-toplevel).Trim()
$dest = Join-Path $repoRoot 'build\package'
New-Item -ItemType Directory -Force $dest | Out-Null
scp -q "${remoteHost}:patchy/src/build/package/Patchy-*.flatpak" $dest
Write-Host "== flatpak bundle copied into $dest =="
Get-ChildItem $dest -Filter 'Patchy-*.flatpak' | ForEach-Object { Write-Host $_.FullName }

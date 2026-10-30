<#
Builds the web release (the wasm site) on a remote Windows build host from the current
working tree, runs the full wasm core suite there, and copies the staged site back into
build\package\wasm-site for upload-wasm-to-rtsoft.bat.

  scripts\remote\release-wasm.ps1 [-SkipTests]

Flow: delete the local build\package\wasm-site, then scripts\remote\remote-build.ps1
-Target wasm (snapshot, host lock, wasm-build-and-test.ps1 on the first idle host in the
config's wasm list), then robocopy the host's build\package\wasm-site and the suite log
back over its 'share' (Windows sshd downloads can stall, so not scp). This keeps the hour-long
wasm-release build and the suite off the machine that builds and signs the Windows
packages. scripts\release\build-wasm.bat stays the local build (the wasm-local target).
#>
param(
  [switch]$SkipTests
)
# 'Continue', not 'Stop': under Windows PowerShell 5.1 any native stderr line becomes a
# terminating NativeCommandError with 'Stop'. Failures are checked explicitly.
$ErrorActionPreference = 'Continue'

. (Join-Path $PSScriptRoot 'remote-hosts.ps1')

$repoRoot = (git rev-parse --show-toplevel).Trim()
if (-not $repoRoot -or -not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) { throw 'could not find the repository root' }
$site = Join-Path $repoRoot 'build\package\wasm-site'
# Delete the previous site up front so a failed run leaves nothing stale for the upload
# script (build-wasm.bat does the same locally).
if (Test-Path -LiteralPath $site) { Remove-Item -LiteralPath (Join-Path $repoRoot 'build\package\wasm-site') -Recurse -Force }
if (Test-Path -LiteralPath $site) { throw "could not delete the previous $site" }

# remote-build.ps1 prints the chosen host (Write-Host, the information stream, hence 6>&1);
# read it back from its output so the copy-back uses that host's share.
$hosts = Get-PatchyWindowsHosts wasm
$buildArgs = @{ Target = 'wasm' }
if ($SkipTests) { $buildArgs.SkipTests = $true }
$chosen = $null
& "$PSScriptRoot\remote-build.ps1" @buildArgs 6>&1 | ForEach-Object {
  $line = "$_"
  Write-Host $line
  if ($line -match '^== building snapshot \S+ on (\S+) ==$') { $chosen = $Matches[1] }
}
$buildExit = $LASTEXITCODE
if ($buildExit -ne 0) { throw "remote wasm build failed (exit $buildExit)" }
$remote = $hosts | Where-Object { $_.ssh -eq $chosen } | Select-Object -First 1
if (-not $remote) { throw "could not tell which wasm host built the site (saw '$chosen')" }

$share = $remote.share.TrimEnd('\')
New-Item -ItemType Directory -Force $site | Out-Null
robocopy (Join-Path $share 'build\package\wasm-site') $site /MIR /NFL /NDL /NJH /NP /R:2 /W:5
if ($LASTEXITCODE -ge 8) { throw "copying the site back from $share failed (robocopy $LASTEXITCODE)" }
if (-not (Test-Path -LiteralPath (Join-Path $site 'patchy.html'))) { throw "robocopy reported success but $site has no patchy.html" }

if (-not $SkipTests) {
  $logs = Join-Path $repoRoot 'build\release-logs'
  New-Item -ItemType Directory -Force $logs | Out-Null
  Copy-Item -LiteralPath (Join-Path $share 'build\wasm-core\wasm-core-tests.log') -Destination (Join-Path $logs 'wasm-core-tests.log') -Force
}
$files = @(Get-ChildItem -LiteralPath $site -Recurse -File)
Write-Host ("== wasm site copied into {0}: {1} files, {2:N0} bytes, built on {3} ==" -f $site, $files.Count, ($files | Measure-Object Length -Sum).Sum, $chosen)
Get-Content -LiteralPath (Join-Path $site 'patchy.html') | Select-String -Pattern '\?v=([0-9.]+-\d+)' | Select-Object -First 1 | ForEach-Object { Write-Host "== cache tag $($_.Matches[0].Groups[1].Value) ==" }
exit 0

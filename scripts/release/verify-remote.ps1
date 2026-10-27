<#
Release verification on the mac or linux build host (docs/release-process.md, "Build and
upload order"):
  1. both full native suites through scripts\remote\remote-build.ps1 (it pushes this working
     tree as a snapshot and builds it there; skip with -SkipSuites),
  2. scripts\release\verify-<target>.sh on the host over ssh: the MCP suites against the
     built and packaged connectors, and on linux the Flatpak and AppImage checks.

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\verify-remote.ps1 -Target mac|linux [-SkipSuites]

Run it after that target's builder (release-all-automated.bat verify-mac / verify-linux do
this unattended); it refuses while build\release-logs\<target>.exit reports a failed build.
Logs: build\release-logs\verify-<target>-suites.log and verify-<target>-checks.log, each with
an .exit marker. A step passes only with exit 0, no [FAIL] line and, for step 2, the script's
closing "ALL ... RELEASE CHECKS PASSED" line. Never name a log verify-<target>.log here: that
is where release-worker.bat redirects this script's own output, so the checks redirect would
fail on the lock and the closing tail would echo the file back into itself without end.

-Target windows-arm64 checks the ARM64 package the windows builder cross-compiled here:
  1. both full suites from a native windows-arm64-opt-dev build on the ARM64 host (the cross
     build cannot run here, and an LTCG link may not fit the host's memory; skip with
     -SkipSuites, which then requires the host's work tree to hold this tree's two
     verification scripts),
  2. PatchyWindowsArm64Installer.exe and PatchyWindowsArm64NoInstaller.zip copied to the
     host and checked there by verify-windows-package.ps1 -Arch arm64 -RequireRun
     -NoDesktop (the wizard step ran on this PC with the windows builder).
Logs: verify-windows-arm64-suites.log and verify-windows-arm64-checks.log.
#>
param(
  [Parameter(Mandatory = $true)][ValidateSet('mac', 'linux', 'windows-arm64')][string]$Target,
  [switch]$SkipSuites
)
# 'Continue', not 'Stop': native stderr would become terminating errors under 5.1.
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'release-process-run.ps1')
$root =(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
if (-not (Test-Path 'CMakeLists.txt')) { throw "not the repository root: $root" }
. (Join-Path $root 'scripts\remote\remote-hosts.ps1')
$remoteHost = (Get-PatchyRemoteHost $Target).ssh
$logs = Join-Path $root 'build\release-logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null

$version = (Select-String -Path 'CMakeLists.txt' -Pattern '^\s*VERSION\s+([0-9.]+)' | Select-Object -First 1).Matches[0].Groups[1].Value
if (-not $version) { throw 'could not read the project version from CMakeLists.txt' }

$builderMarker = Join-Path $logs $(if ($Target -eq 'windows-arm64') { 'windows.exit' } else { "$Target.exit" })
if (Test-Path $builderMarker) {
  $builder = (Get-Content $builderMarker -Raw).Trim()
  if ($builder -ne 'exit=0') { Write-Output "$Target builder reported $builder; not verifying."; exit 1 }
}

function Write-Marker([string]$log, [int]$code) {
  [IO.File]::WriteAllText([IO.Path]::ChangeExtension($log, '.exit'), "exit=$code`r`n")
}
function Count([string]$log, [string]$text) {
  return @(Select-String -LiteralPath $log -SimpleMatch $text -ErrorAction SilentlyContinue).Count
}

if (-not $SkipSuites) {
  $suitesLog = Join-Path $logs "verify-$Target-suites.log"
  Write-Output "== $Target full native suites (remote-build.ps1) =="
  $presetArgument = $(if ($Target -eq 'windows-arm64') { ' -Preset windows-arm64-opt-dev' } else { '' })
  $code = Invoke-CmdLine ('powershell -NoProfile -ExecutionPolicy Bypass -File scripts\remote\remote-build.ps1 -Target ' +
                         $Target + $presetArgument + ' > "' + $suitesLog + '" 2>&1')
  $fails = (Count $suitesLog '[FAIL]') + (Count $suitesLog '[CRASH]')
  if ($code -eq 0 -and $fails -gt 0) { $code = 1 }
  Write-Marker $suitesLog $code
  Write-Output ("suites: exit={0} pass={1} fail={2}" -f $code, (Count $suitesLog '[PASS]'), $fails)
  if ($code -ne 0) { exit $code }
}

if ($Target -eq 'windows-arm64') {
  . (Join-Path $root 'scripts\remote\remote-lock.ps1')
  $remote = Get-PatchyRemoteHost windows-arm64
  $workTree = $remote.workTree.Replace('\', '/').TrimEnd('/')
  $packageDir = $workTree + '/build/package/arm64-release-check'
  $log = Join-Path $logs 'verify-windows-arm64-checks.log'
  $packages = @('build\package\PatchyWindowsArm64Installer.exe', 'build\package\PatchyWindowsArm64NoInstaller.zip')
  foreach ($package in $packages) {
    if (-not (Test-Path $package)) { Write-Output "missing $package (run the windows builder first)"; exit 1 }
  }
  $lockPath = Get-PatchyLockPath $true $remote
  $lockOwner = Get-PatchyLockOwner $root
  $why = Enter-PatchyRemoteLock $remoteHost $true $lockPath $lockOwner
  if ($why) { Write-Output "== $why =="; exit 75 }
  try {
    # The checks run from the host's work tree: it must hold this tree's scripts (the suites
    # step above just pushed it there).
    $scripts = @('scripts/release/verify-windows-package.ps1', 'packaging/package-selftest.js')
    $stale = @()
    foreach ($relative in $scripts) {
      $want = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root $relative)).Hash
      $have = (ssh -o ConnectTimeout=30 $remoteHost ("(Get-FileHash -Algorithm SHA256 -LiteralPath '" + $workTree + '/' + $relative + "').Hash") 2>$null | Select-Object -Last 1)
      if ("$have".Trim() -ne $want) { $stale += $relative }
    }
    if ($stale.Count -gt 0) {
      Write-Output ("the host's work tree has other copies of " + ($stale -join ', ') + '; run without -SkipSuites')
      exit 1
    }
    Write-Output "== windows-arm64 package checks (verify-windows-package.ps1 -Arch arm64 on $remoteHost) =="
    $code = Invoke-CmdLine ('ssh -o ConnectTimeout=30 ' + $remoteHost + ' "New-Item -ItemType Directory -Force -Path ''' +
                           $packageDir + ''' | Out-Null" > "' + $log + '" 2>&1')
    foreach ($package in $packages) {
      if ($code -eq 0) {
        $code = Invoke-CmdLine ('scp -q "' + $package + '" "' + $remoteHost + ':' + $packageDir + '/" >> "' + $log + '" 2>&1')
      }
    }
    if ($code -eq 0) {
      $command = 'powershell -NoProfile -ExecutionPolicy Bypass -File ''' + $workTree + '/scripts/release/verify-windows-package.ps1'' -Arch arm64' +
        ' -Installer ''' + $packageDir + '/PatchyWindowsArm64Installer.exe'' -Zip ''' + $packageDir + '/PatchyWindowsArm64NoInstaller.zip''' +
        ' -Version ' + $version + ' -NoDesktop -RequireRun; exit $LASTEXITCODE'
      $code = Invoke-CmdLine ('ssh -o ConnectTimeout=30 ' + $remoteHost + ' "' + $command + '" >> "' + $log + '" 2>&1')
    }
  } finally {
    Exit-PatchyRemoteLock $remoteHost $true $lockPath $lockOwner
  }
  $fails = Count $log 'FAIL  '
  $finished = (Count $log 'package verification passed') -gt 0
  if ($code -eq 0 -and ($fails -gt 0 -or -not $finished)) { $code = 1 }
  Write-Marker $log $code
  Write-Output ("checks: exit={0} pass={1} fail={2}" -f $code, (Count $log 'PASS  '), $fails)
  Get-Content -LiteralPath $log -Tail 4
  exit $code
}

# The host script goes over ssh's stdin with LF line ends: a CR would reach bash as part of
# each command.
$script = (Get-Content (Join-Path $PSScriptRoot "verify-$Target.sh") -Raw) -replace "`r", ''
$scriptCopy = Join-Path $logs "verify-$Target.sh.tmp"
[IO.File]::WriteAllText($scriptCopy, $script, (New-Object System.Text.UTF8Encoding($false)))
$log = Join-Path $logs "verify-$Target-checks.log"
Write-Output "== $Target release checks (verify-$Target.sh $version on $remoteHost) =="
$code = Invoke-CmdLine ('ssh -o ConnectTimeout=30 ' + $remoteHost + ' "bash -s -- ' + $version + '" < "' +
                       $scriptCopy + '" > "' + $log + '" 2>&1')
$fails = Count $log '[FAIL]'
$finished = (Count $log 'RELEASE CHECKS PASSED') -gt 0
if ($code -eq 0 -and ($fails -gt 0 -or -not $finished)) { $code = 1 }
Write-Marker $log $code
Write-Output ("checks: exit={0} pass={1} fail={2}" -f $code, (Count $log '[PASS]'), $fails)
Get-Content -LiteralPath $log -Tail 4
exit $code

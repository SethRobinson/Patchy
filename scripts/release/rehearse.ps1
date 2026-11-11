<#
Release rehearsal for one platform, run on its build host before a version bump
(docs/release-process.md, "Before the version bump"). It finds what the release rounds kept
finding on release day, with no package, signature or bump:

  1. the warning check (warncheck.ps1 -Target <target>),
  2. both full native suites with the expected-skip check (verify-remote.ps1 -SuitesOnly).

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\rehearse.ps1 -Target windows|windows-arm64|mac|linux [-Clean]

Step 2 runs even when step 1 fails, so one pass reports both kinds of problem. The web
build has no suite rehearsal: warncheck-wasm covers its compile. Exit 0 only when both
steps passed. The release-all-automated.bat targets rehearse-<target> (and rehearse, all
four plus warncheck-wasm) run this unattended; the suite log is
build\release-logs\rehearse-<target>-suites.log, beside the candidate expected-skip lists a
host without lists gets.
#>
param(
  [Parameter(Mandatory = $true)][ValidateSet('windows', 'windows-arm64', 'mac', 'linux')][string]$Target,
  [switch]$Clean
)
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
if (-not (Test-Path 'CMakeLists.txt')) { throw "not the repository root: $root" }

$start = Get-Date
& (Join-Path $PSScriptRoot 'warncheck.ps1') -Target $Target -Clean:$Clean
$warnExit = $LASTEXITCODE
$suitesStart = Get-Date
& (Join-Path $PSScriptRoot 'verify-remote.ps1') -Target $Target -SuitesOnly
$suitesExit = $LASTEXITCODE
$end = Get-Date

function State([int]$code) { switch ($code) { 0 { 'passed' } 75 { 'not run (host busy or locked)' } default { "FAILED (exit $code)" } } }
Write-Output "== rehearse $Target summary =="
Write-Output ("  warning check: {0}, {1:N0} min" -f (State $warnExit), ($suitesStart - $start).TotalMinutes)
Write-Output ("  full suites:   {0}, {1:N0} min (log build\release-logs\rehearse-{2}-suites.log)" -f (State $suitesExit), ($end - $suitesStart).TotalMinutes, $Target)
if ($warnExit -eq 0 -and $suitesExit -eq 0) { Write-Output "== REHEARSAL $Target PASSED =="; exit 0 }
Write-Output "== REHEARSAL $Target FAILED =="
if ($warnExit -ne 0 -and $warnExit -ne 75) { exit 1 }
if ($suitesExit -ne 0 -and $suitesExit -ne 75) { exit 1 }
exit 75

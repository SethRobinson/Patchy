<#
Windows half of scripts\remote\remote-build.ps1 -Target windows. Runs inside the
work tree right after the snapshot checkout: configure + build the preset in the VS
developer environment, then run both test suites. Mirrors build-and-test.sh.

  build-and-test.ps1 [-Preset release] [-Filter <ui substr>] [-CoreFilter <core substr>]
                     [-SkipTests] [-Jobs 8]

Everything heavy goes through scripts\run-throttled.bat (below-normal priority, real exit
codes, crash codes included), exactly like the dev box's release handoff, so the build
machine stays usable for whatever else it does. -Jobs 8 suits a 12-thread / 16 GB box;
drop it to 6 if the LTCG links run short of memory.
#>
param(
  [string]$Preset = 'release',
  [string]$Filter = '',
  [string]$CoreFilter = '',
  [switch]$SkipTests,
  [int]$Jobs = 8
)

$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $repo
$vsEnv = 'scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul'
$buildDir = Join-Path $repo "build\$Preset"

Write-Host "== configure ($Preset) =="
cmd /s /c "$vsEnv && cmake --preset $Preset"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "== build ($Preset, -j $Jobs, below-normal priority) =="
cmd /s /c "$vsEnv && scripts\run-throttled.bat cmake --build --preset $Preset -j $Jobs"
$buildExit = $LASTEXITCODE
Write-Host "patchy.exe exists: $(Test-Path (Join-Path $buildDir 'patchy.exe'))"
if ($buildExit -ne 0) { exit $buildExit }

if ($SkipTests) {
  Write-Host '== tests skipped =='
  exit 0
}

$firstFailure = 0
$throttled = Join-Path $repo 'scripts\run-throttled.bat'
# Argument arrays, not interpolated quotes: Windows PowerShell 5.1 cannot parse nested
# quotes inside $() and mangles embedded double quotes passed to native commands.
$coreArgs = @()
if ($CoreFilter) { $coreArgs = @($CoreFilter) }
$uiArgs = @()
if ($Filter) { $uiArgs = @($Filter) }

Push-Location $buildDir
try {
  Write-Host "== patchy_core_tests $CoreFilter =="
  & $throttled .\patchy_core_tests.exe @coreArgs
  if ($LASTEXITCODE -ne 0 -and $firstFailure -eq 0) { $firstFailure = $LASTEXITCODE }

  Write-Host "== patchy_ui_visual_tests (offscreen) $Filter =="
  $env:QT_QPA_PLATFORM = 'offscreen'
  & $throttled .\patchy_ui_visual_tests.exe @uiArgs
  if ($LASTEXITCODE -ne 0 -and $firstFailure -eq 0) { $firstFailure = $LASTEXITCODE }
}
finally {
  Pop-Location
}

exit $firstFailure

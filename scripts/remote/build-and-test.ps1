<#
Windows half of scripts\remote\remote-build.ps1 -Target windows. Runs inside the
work tree right after the snapshot checkout: configure + build the preset in the VS
developer environment, then run the test suites. Mirrors build-and-test.sh.

  build-and-test.ps1 [-Preset release] [-BuildTarget <target>[,<target>...]]
                     [-Filter <ui substr>] [-CoreFilter <core substr>] [-SkipTests] [-Jobs 8]
                     [-PerfScenario startup] [-PerfEnv 'NAME=value;NAME=value']

-BuildTarget builds only the named CMake targets instead of the preset's default target,
and then only the suites whose executables this run built are run: patchy_core_tests and
patchy_ui_visual_tests each run only when named. A suite that was not built is reported
and skipped, never run from whatever older binary the build directory still holds.

Everything heavy goes through scripts\run-throttled.bat (below-normal priority, real exit
codes, crash codes included, and the machine-wide build lock), exactly like the dev box's
release handoff, so the build machine stays usable for whatever else it does. -Jobs 8 suits
a 12-thread / 16 GB box; drop it to 6 if the LTCG links run short of memory.

The build output is also written to build\<preset>\remote-build.log, and the run ends the
build with a count of the warning lines it printed. Ninja only prints what it compiled or
linked in this run, so that count covers exactly the targets built and only the sources
that changed: zero-warning verification still needs the release preset's default target
from a clean build directory (docs/platform.md, "Warning verification").

-PerfScenario runs `patchy_perf_tests <scenario>` after the suites (alone with -SkipTests)
when this run built patchy_perf_tests, with each -PerfEnv NAME=value set first; `startup` is
the startup timing and regression check (docs/startup-performance.md).
#>
param(
  [string]$Preset = 'release',
  [string]$BuildTarget = '',
  [string]$Filter = '',
  [string]$CoreFilter = '',
  [switch]$SkipTests,
  [string]$PerfScenario = '',
  [string]$PerfEnv = '',
  [int]$Jobs = 8
)

$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $repo
# The windows-arm64-* presets build natively on an ARM64 host (its Qt kit is msvc2022_arm64);
# every other Windows preset is x64.
$vsArch = if ($Preset -like 'windows-arm64*') { 'arm64' } else { 'x64' }
$vsEnv = "scripts\vs-env.bat -arch=$vsArch -host_arch=$vsArch >nul"
$buildDir = Join-Path $repo "build\$Preset"
$targets = @($BuildTarget -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$targetArgs = ''
$targetNote = 'default target'
if ($targets.Count) {
  $targetArgs = ' --target ' + ($targets -join ' ')
  $targetNote = 'targets ' + ($targets -join ', ')
}

# Tee-Object streams every line as it arrives; $LASTEXITCODE still comes from cmd. The log
# holds configure and build output, so a CMake warning counts too.
New-Item -ItemType Directory -Force $buildDir | Out-Null
$buildLog = Join-Path $buildDir 'remote-build.log'
Write-Host "== configure ($Preset) =="
cmd /s /c "$vsEnv && cmake --preset $Preset 2>&1" | Tee-Object -FilePath $buildLog
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "== build ($Preset, $targetNote, -j $Jobs, below-normal priority) =="
cmd /s /c "$vsEnv && scripts\run-throttled.bat cmake --build --preset $Preset -j $Jobs$targetArgs 2>&1" | Tee-Object -FilePath $buildLog -Append
$buildExit = $LASTEXITCODE
# MSVC (C4xxx), linker (LNKxxxx), command-line (D9xxx), CMake and lrelease warnings all carry
# the word; the ninja progress lines name objects, never "warning".
$warnings = @(Get-Content $buildLog -ErrorAction SilentlyContinue | Where-Object { $_ -match '(?i)\bwarning\b' })
Write-Host "== warnings printed by this build: $($warnings.Count) ($Preset, $targetNote; only what this run compiled and linked) =="
$warnings | Select-Object -Unique -First 40 | ForEach-Object { Write-Host "  $_" }
if (-not $targets.Count -or $targets -contains 'patchy') {
  Write-Host "patchy.exe exists: $(Test-Path (Join-Path $buildDir 'patchy.exe'))"
}
if ($buildExit -ne 0) { exit $buildExit }

if ($SkipTests -and -not $PerfScenario) {
  Write-Host '== tests skipped =='
  exit 0
}

$runCore = (-not $SkipTests) -and ((-not $targets.Count) -or ($targets -contains 'patchy_core_tests'))
$runUi = (-not $SkipTests) -and ((-not $targets.Count) -or ($targets -contains 'patchy_ui_visual_tests'))
$runPerf = $PerfScenario -and ((-not $targets.Count) -or ($targets -contains 'patchy_perf_tests'))
if (-not $runCore -and -not $runUi -and -not $runPerf) {
  Write-Host '== no test suite among the built targets; tests skipped =='
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
  if ($runCore) {
    Write-Host "== patchy_core_tests $CoreFilter =="
    & $throttled .\patchy_core_tests.exe @coreArgs
    if ($LASTEXITCODE -ne 0 -and $firstFailure -eq 0) { $firstFailure = $LASTEXITCODE }
  }
  elseif (-not $SkipTests) { Write-Host '== patchy_core_tests not built in this run; skipped ==' }

  if ($runUi) {
    Write-Host "== patchy_ui_visual_tests (offscreen) $Filter =="
    $env:QT_QPA_PLATFORM = 'offscreen'
    & $throttled .\patchy_ui_visual_tests.exe @uiArgs
    if ($LASTEXITCODE -ne 0 -and $firstFailure -eq 0) { $firstFailure = $LASTEXITCODE }
  }
  elseif (-not $SkipTests) { Write-Host '== patchy_ui_visual_tests not built in this run; skipped ==' }

  if ($runPerf) {
    foreach ($pair in ($PerfEnv -split ';')) {
      if ($pair -match '^\s*([A-Za-z_][A-Za-z0-9_]*)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
    Write-Host "== patchy_perf_tests $PerfScenario ($PerfEnv) =="
    & $throttled .\patchy_perf_tests.exe $PerfScenario
    if ($LASTEXITCODE -ne 0 -and $firstFailure -eq 0) { $firstFailure = $LASTEXITCODE }
  }
  elseif ($PerfScenario) { Write-Host '== patchy_perf_tests not built in this run; skipped ==' }
}
finally {
  Pop-Location
}

exit $firstFailure

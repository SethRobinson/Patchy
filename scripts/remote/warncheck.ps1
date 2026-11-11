<#
The warning check of docs/platform.md ("Warning verification") for Windows-hosted builds:
build-and-test.ps1 -Warncheck (x64, ARM64) and wasm-build-and-test.ps1 -Warncheck (the web
app and wasm-core) run it. scripts/remote/warncheck.sh is its twin for mac and Linux, and
the two behave the same:

  warncheck.ps1 -Preset <preset> -EnvCommand '<cmd prefix>' [-Jobs 8] [-Clean]

Builds the preset's default target in its own build\warncheck-<preset> (never the preset's
warm build directory), then fails when the configure or build printed any warning line.
-EnvCommand is the cmd line that sets up the toolchain before cmake runs (vs-env.bat, plus
emsdk_env.bat for wasm). The folder is kept after a pass, so the next run compiles only what
changed since: every warning a change brings is printed by the first run that compiles it.
Anything that could hide one starts the next run from an empty folder: a run that did not
pass (its warned objects are already built and would not be printed again), a changed
compiler, Qt or Emscripten (a toolchain update warns in unchanged files), the first run, or
-Clean. build\warncheck-<preset>.passed holds the toolchain fingerprint of the last pass;
build\warncheck-<preset>.log the last run's output.
#>
param(
  [Parameter(Mandatory = $true)][string]$Preset,
  [Parameter(Mandatory = $true)][string]$EnvCommand,
  [int]$Jobs = 8,
  [switch]$Clean
)
$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $repo
if (-not (Test-Path -LiteralPath (Join-Path $repo 'CMakeLists.txt'))) { Write-Host "not the repository root: $repo"; exit 2 }
if ($Preset -notmatch '^[A-Za-z0-9_.+-]+$') { Write-Host "bad preset name: $Preset"; exit 2 }

$dir = Join-Path $repo "build\warncheck-$Preset"
$log = Join-Path $repo "build\warncheck-$Preset.log"
$passed = Join-Path $repo "build\warncheck-$Preset.passed"
# MSVC (C4xxx), linker (LNKxxxx), command-line (D9xxx), clang, CMake and lrelease warnings all
# carry the word. Ninja's own [N/M] progress lines name objects (no_layer_warning_tests.cpp),
# never a diagnostic, and \b already keeps an underscore name whole.
$warningPattern = '(?i)\bwarning\b'
$progressPattern = '^\[\d+/\d+\] '

function Get-Fingerprint([string]$cache) {
  $parts = New-Object System.Collections.Generic.List[string]
  if (Test-Path -LiteralPath $cache) {
    $lines = Get-Content -LiteralPath $cache
    $cxx = ($lines | Where-Object { $_ -match '^CMAKE_CXX_COMPILER:[A-Z]+=' } | Select-Object -First 1) -replace '^[^=]+=', ''
    $qt = ($lines | Where-Object { $_ -match '^Qt6_DIR:[A-Z]+=' } | Select-Object -First 1) -replace '^[^=]+=', ''
    $parts.Add("cxx=$cxx")
    # MSVC's path names its toolset version; size and time catch an update in place.
    if ($cxx -and (Test-Path -LiteralPath $cxx)) {
      $item = Get-Item -LiteralPath $cxx
      $parts.Add("$($item.Length) $($item.LastWriteTimeUtc.Ticks)")
    }
    $parts.Add("qt=$qt")
    if ($qt) {
      $qtVersion = Join-Path $qt 'Qt6ConfigVersion.cmake'
      if (Test-Path -LiteralPath $qtVersion) { $parts.Add((Select-String -LiteralPath $qtVersion -Pattern 'PACKAGE_VERSION' | Select-Object -First 1).Line) }
    }
  }
  $emscripten = Join-Path $repo '.deps\emsdk\upstream\emscripten\emscripten-version.txt'
  if ($cxx -match 'emscripten|em\+\+' -and (Test-Path -LiteralPath $emscripten)) { $parts.Add((Get-Content -Raw -LiteralPath $emscripten).Trim()) }
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $bytes = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($parts -join "`n")))
  return ([BitConverter]::ToString($bytes) -replace '-', '').Substring(0, 16)
}

$reason = ''
if ($Clean) { $reason = 'asked for a clean build' }
elseif (-not (Test-Path -LiteralPath (Join-Path $dir 'CMakeCache.txt'))) { $reason = 'first run' }
elseif (-not (Test-Path -LiteralPath $passed)) { $reason = 'the last run did not pass' }
elseif ((Get-Fingerprint (Join-Path $dir 'CMakeCache.txt')) -ne (Get-Content -Raw -LiteralPath $passed).Trim()) {
  $reason = 'the compiler or Qt changed since the last pass'
}
if (Test-Path -LiteralPath $passed) { Remove-Item -LiteralPath $passed -Force }
if ($reason) {
  Write-Host "== warncheck ($Preset): clean build, $reason =="
  if (Test-Path -LiteralPath $dir) {
    Remove-Item -LiteralPath (Join-Path $repo "build\warncheck-$Preset") -Recurse -Force
    if (Test-Path -LiteralPath $dir) { Write-Host "could not delete $dir"; exit 1 }
  }
} else {
  Write-Host "== warncheck ($Preset): incremental, the last run passed with this compiler and Qt =="
}
New-Item -ItemType Directory -Force (Join-Path $repo 'build') | Out-Null

$start = Get-Date
Write-Host "== warncheck ($Preset): configure into build\warncheck-$Preset, then the default target, -j $Jobs, below-normal priority =="
cmd /s /c "$EnvCommand && cmake --preset $Preset -B build\warncheck-$Preset 2>&1" | Tee-Object -FilePath $log
$buildExit = $LASTEXITCODE
if ($buildExit -eq 0) {
  cmd /s /c "$EnvCommand && scripts\run-throttled.bat cmake --build build\warncheck-$Preset -j $Jobs 2>&1" | Tee-Object -FilePath $log -Append
  $buildExit = $LASTEXITCODE
}
$minutes = [int][Math]::Ceiling(((Get-Date) - $start).TotalMinutes)
$lines = @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue)
$steps = @($lines | Where-Object { $_ -match $progressPattern }).Count
$warnings = @($lines | Where-Object { $_ -notmatch $progressPattern -and $_ -match $warningPattern })
$warnings | Select-Object -Unique -First 40 | ForEach-Object { Write-Host "  $_" }
Write-Host "== warncheck ($Preset): $($warnings.Count) warning lines, $steps build steps, $minutes min, log $log =="
if ($buildExit -ne 0) { Write-Host "== WARNCHECK FAILED ($Preset): the build exited $buildExit =="; exit 1 }
if ($warnings.Count) { Write-Host "== WARNCHECK FAILED ($Preset): $($warnings.Count) warning lines =="; exit 1 }
[IO.File]::WriteAllText($passed, (Get-Fingerprint (Join-Path $dir 'CMakeCache.txt')))
Write-Host "== WARNCHECK PASSED ($Preset) =="
exit 0

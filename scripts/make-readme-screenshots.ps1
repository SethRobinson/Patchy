# Regenerates the README marketing screenshots.
#
# Builds the UI visual test binary, runs the shot_readme_* scenes offscreen
# (see the "README screenshots" section in tests/ui_visual_tests.cpp), and
# copies the resulting PNGs into docs/images/screenshots/ with the
# shot_readme_ prefix stripped (the names README.md links to).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\make-readme-screenshots.ps1 [-SkipBuild]
#
# Some scenes need local fixtures in local-test-fixtures/psd/ (not committed:
# akiko_cycling_okinawa.jpg, ipad_main_v04.psd, mow_master.psd); scenes whose
# fixture is missing are skipped with a [SKIP] line.
param([switch]$SkipBuild)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if (-not $SkipBuild) {
    Push-Location $repo
    try {
        cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset release --target patchy_ui_visual_tests'
        if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
    } finally {
        Pop-Location
    }
}

$buildDir = Join-Path $repo 'build\release'
$exe = Join-Path $buildDir 'patchy_ui_visual_tests.exe'
if (-not (Test-Path $exe)) { throw "missing $exe (run without -SkipBuild)" }

Push-Location $buildDir
try {
    $env:QT_QPA_PLATFORM = 'offscreen'
    & $exe shot_readme
    if ($LASTEXITCODE -ne 0) { throw "screenshot scenes failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}

$outDir = Join-Path $repo 'docs\images\screenshots'
New-Item -ItemType Directory -Force $outDir | Out-Null
$shots = Get-ChildItem (Join-Path $buildDir 'test-artifacts\shot_readme_*.png')
if ($shots.Count -eq 0) { throw 'no shot_readme_*.png artifacts were produced' }
foreach ($shot in $shots) {
    $dest = Join-Path $outDir ($shot.Name -replace '^shot_readme_', '')
    Copy-Item $shot.FullName $dest -Force
    Write-Host "updated $dest"
}

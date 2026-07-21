# Regenerates the README marketing screenshots.
#
# Builds the UI visual test binary, runs the shot_readme_* scenes offscreen
# (see the "README screenshots" section in tests/ui/readme_screenshot_tests.cpp), and
# copies the resulting PNGs into docs/images/screenshots/ with the
# shot_readme_ prefix stripped (the names README.md links to).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\make-readme-screenshots.ps1 [-SkipBuild] [-Scene camera_raw]
#
# Some scenes need local fixtures in local-test-fixtures/psd/ (not committed:
# akiko_cycling_okinawa.jpg, ipad_main_v04.psd, mow_master.psd); scenes whose
# fixture is missing are skipped with a [SKIP] line. The Camera Raw scene uses
# local-test-fixtures/raw/fujifilm_xt1.raf, a CC0 sample from raw.pixls.us. The
# Tilt-Shift scene uses the committed CC0 San Francisco photograph and the SVG
# import scene the committed CC0 hot-air-balloon clip art, both documented in
# NOTICE-THIRD-PARTY.md. The Affinity import scene uses the local
# local-test-fixtures/af-spike/corpus/tips.af document.
param(
    [switch]$SkipBuild,
    [string]$Scene = 'shot_readme'
)

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
$filter = if ($Scene.StartsWith('shot_readme')) { $Scene } else { "shot_readme_$Scene" }
$artifactPattern = "$filter*.png"
$artifactDir = Join-Path $buildDir 'test-artifacts'
if (Test-Path $artifactDir) {
    Get-ChildItem (Join-Path $artifactDir $artifactPattern) -ErrorAction SilentlyContinue |
        Remove-Item -Force
}

Push-Location $buildDir
try {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:PATCHY_NO_SOUND = '1'
    & $exe $filter
    if ($LASTEXITCODE -ne 0) { throw "screenshot scenes failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}

$outDir = Join-Path $repo 'docs\images\screenshots'
New-Item -ItemType Directory -Force $outDir | Out-Null
$shots = @(Get-ChildItem (Join-Path $artifactDir $artifactPattern) -ErrorAction SilentlyContinue)
if ($shots.Count -eq 0) { throw "no $artifactPattern artifacts were produced" }
foreach ($shot in $shots) {
    $dest = Join-Path $outDir ($shot.Name -replace '^shot_readme_', '')
    Copy-Item $shot.FullName $dest -Force
    Write-Host "updated $dest"
}

# One-time (idempotent) provisioning of the Qt for WebAssembly kit for the
# wasm-release preset. Installs Qt $QtVersion wasm_multithread (plus
# qtimageformats) into .deps/Qt/<version>/wasm_multithread via aqtinstall, next
# to the existing desktop kits, and installs the matching win64_msvc2022_64
# host kit when it is missing: QT_HOST_PATH must be the same Qt version as the
# wasm kit (moc/rcc/lrelease and the qtbase_ja.qm the build stages both come
# from it). Run scripts\wasm\setup-emsdk.ps1 first; Qt 6.10/6.11 pair with the
# Emscripten 4.0.7 that script pins.
#
# 6.10.3, not 6.11.x: released aqtinstall (3.3.0) cannot install a 6.11 desktop
# host kit at all. download.qt.io restructured the 6.11 desktop repo into
# per-arch folders (qt6_6111_msvc2022_64) with no base qt6_6111/Updates.xml,
# and aqt still requests the base file; 6.10.3 keeps the old layout. Revisit
# 6.11+ once aqtinstall understands the new desktop layout.
#
# Run from anywhere:
#   pwsh -File scripts\wasm\setup-qt-wasm.ps1
# Rerunning is a fast no-op once everything is installed. Pass -WasmArch
# wasm_singlethread to provision the single-threaded kit instead, or -QtVersion
# to install a different release (kits coexist under .deps/Qt/<version>/).
param(
  [string]$WasmArch = 'wasm_multithread',
  [string]$QtVersion = '6.10.3'
)
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$QtRoot = Join-Path $RepoRoot '.deps\Qt'
$WasmKitDir = Join-Path $QtRoot "$QtVersion\$WasmArch"
$VenvDir = Join-Path $RepoRoot '.deps\aqt-venv'
$HostKitDir = Join-Path $QtRoot "$QtVersion\msvc2022_64"

Write-Host "== Patchy Qt wasm kit setup (Qt $QtVersion $WasmArch) =="

if (-not (Test-Path (Join-Path $VenvDir 'Scripts\aqt.exe'))) {
  Write-Host "== Creating aqtinstall venv at $VenvDir =="
  $Python = (Get-Command python -ErrorAction SilentlyContinue) ??
            (Get-Command python3 -ErrorAction SilentlyContinue)
  if (-not $Python) { throw "No python found on PATH to create the aqt venv" }
  & $Python.Source -m venv $VenvDir
}
# python -m pip, not pip.exe: on Windows pip cannot replace its own exe. Always
# upgrade: an old aqtinstall may predate the requested Qt release's metadata.
& (Join-Path $VenvDir 'Scripts\python.exe') -m pip install --quiet --upgrade pip aqtinstall
if ($LASTEXITCODE -ne 0) { throw "pip install aqtinstall failed" }
$Aqt = Join-Path $VenvDir 'Scripts\aqt.exe'

if (-not (Test-Path (Join-Path $HostKitDir 'bin\qmake.exe'))) {
  Write-Host "== Installing Qt $QtVersion win64_msvc2022_64 host kit (QT_HOST_PATH) =="
  & $Aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -O $QtRoot
  if ($LASTEXITCODE -ne 0) { throw "aqt install-qt (host kit) failed" }
}

if (-not (Test-Path (Join-Path $WasmKitDir 'lib\cmake\Qt6\qt.toolchain.cmake'))) {
  Write-Host "== Installing Qt $QtVersion $WasmArch (downloads ~1 GB) =="
  & $Aqt install-qt all_os wasm $QtVersion $WasmArch -m qtimageformats -O $QtRoot
  if ($LASTEXITCODE -ne 0) { throw "aqt install-qt failed" }
}

if (-not (Test-Path (Join-Path $WasmKitDir 'lib\cmake\Qt6\qt.toolchain.cmake'))) {
  throw "qt.toolchain.cmake missing after install; the kit layout is not what the wasm-release preset expects"
}
# The build depends on more than qmake from the host kit: lrelease compiles the
# app translations and translations\qtbase_ja.qm is staged into the wasm image
# (CMakeLists silently skips it when absent, shipping English-only Qt strings).
foreach ($Probe in @('bin\qmake.exe', 'bin\lrelease.exe', 'translations\qtbase_ja.qm')) {
  if (-not (Test-Path (Join-Path $HostKitDir $Probe))) {
    throw "Host kit is incomplete: $Probe missing under $HostKitDir"
  }
}

Write-Host "== Versions =="
& $Aqt version
Write-Host "wasm kit: $WasmKitDir OK"
Write-Host "host kit: $HostKitDir OK"
Write-Host "== Qt wasm kit setup complete =="

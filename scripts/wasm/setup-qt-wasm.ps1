# One-time (idempotent) provisioning of the Qt for WebAssembly kit for the
# wasm-release preset. Installs Qt 6.8.3 wasm_multithread (plus qtimageformats)
# into .deps/Qt/6.8.3/wasm_multithread via aqtinstall, next to the existing
# desktop kits. Host tools (moc/rcc/lrelease) come from the already-vendored
# .deps/Qt/6.8.3/msvc2022_64 through QT_HOST_PATH, so no --autodesktop install
# is needed. Run scripts\wasm\setup-emsdk.ps1 first; Qt 6.8 pairs with the
# Emscripten 3.1.56 that script pins.
#
# Run from anywhere:
#   pwsh -File scripts\wasm\setup-qt-wasm.ps1
# Rerunning is a fast no-op once everything is installed. Pass -WasmArch
# wasm_singlethread to provision the old single-threaded kit instead (kits
# coexist under .deps/Qt/6.8.3/).
param(
  [string]$WasmArch = 'wasm_multithread'
)
$ErrorActionPreference = 'Stop'

$QtVersion = '6.8.3'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$QtRoot = Join-Path $RepoRoot '.deps\Qt'
$WasmKitDir = Join-Path $QtRoot "$QtVersion\$WasmArch"
$VenvDir = Join-Path $RepoRoot '.deps\aqt-venv'
$HostKitDir = Join-Path $QtRoot "$QtVersion\msvc2022_64"

Write-Host "== Patchy Qt wasm kit setup (Qt $QtVersion $WasmArch) =="

if (-not (Test-Path (Join-Path $HostKitDir 'bin\qmake.exe'))) {
  throw "Host Qt kit not found at $HostKitDir (needed as QT_HOST_PATH)"
}

if (-not (Test-Path (Join-Path $VenvDir 'Scripts\aqt.exe'))) {
  Write-Host "== Creating aqtinstall venv at $VenvDir =="
  $Python = (Get-Command python -ErrorAction SilentlyContinue) ??
            (Get-Command python3 -ErrorAction SilentlyContinue)
  if (-not $Python) { throw "No python found on PATH to create the aqt venv" }
  & $Python.Source -m venv $VenvDir
  # python -m pip, not pip.exe: on Windows pip cannot replace its own exe.
  & (Join-Path $VenvDir 'Scripts\python.exe') -m pip install --quiet --upgrade pip aqtinstall
  if ($LASTEXITCODE -ne 0) { throw "pip install aqtinstall failed" }
}

if (-not (Test-Path (Join-Path $WasmKitDir 'lib\cmake\Qt6\qt.toolchain.cmake'))) {
  Write-Host "== Installing Qt $QtVersion $WasmArch (downloads ~1 GB) =="
  & (Join-Path $VenvDir 'Scripts\aqt.exe') install-qt all_os wasm $QtVersion $WasmArch -m qtimageformats -O $QtRoot
  if ($LASTEXITCODE -ne 0) { throw "aqt install-qt failed" }
}

if (-not (Test-Path (Join-Path $WasmKitDir 'lib\cmake\Qt6\qt.toolchain.cmake'))) {
  throw "qt.toolchain.cmake missing after install; the kit layout is not what the wasm-release preset expects"
}

Write-Host "== Versions =="
& (Join-Path $VenvDir 'Scripts\aqt.exe') version
Write-Host "wasm kit: $WasmKitDir OK"
Write-Host "host kit: $HostKitDir OK"
Write-Host "== Qt wasm kit setup complete =="

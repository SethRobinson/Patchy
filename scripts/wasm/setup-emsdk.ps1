# One-time (idempotent) provisioning of the Emscripten toolchain for Patchy
# WebAssembly builds. Installs emsdk pinned to 3.1.56 into .deps/emsdk (already
# gitignored, same layout idea as .deps/Qt) and activates it locally: activation
# only writes emsdk's own config files, never the user or system environment.
#
# 3.1.56 is the Emscripten version the Qt 6.8 documentation lists as supported
# for Qt for WebAssembly, so the later Qt-for-wasm kit can reuse this toolchain
# without a rebuild-everything version change.
#
# Run from anywhere:
#   pwsh -File scripts\wasm\setup-emsdk.ps1
# Rerunning is a fast no-op once everything is installed.
$ErrorActionPreference = 'Stop'

$EmsdkVersion = '3.1.56'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$EmsdkDir = Join-Path $RepoRoot '.deps\emsdk'
$EmsdkBat = Join-Path $EmsdkDir 'emsdk.bat'

Write-Host "== Patchy wasm toolchain setup (emsdk $EmsdkVersion) =="

if (-not (Test-Path $EmsdkBat)) {
  Write-Host "== Cloning emsdk into $EmsdkDir =="
  git clone https://github.com/emscripten-core/emsdk.git $EmsdkDir
  if ($LASTEXITCODE -ne 0) { throw "git clone of emsdk failed" }
}

# Both commands are idempotent: install skips already-downloaded components and
# activate rewrites .deps\emsdk\.emscripten (a local file) with the pinned paths.
& $EmsdkBat install $EmsdkVersion
if ($LASTEXITCODE -ne 0) { throw "emsdk install $EmsdkVersion failed" }
& $EmsdkBat activate $EmsdkVersion
if ($LASTEXITCODE -ne 0) { throw "emsdk activate $EmsdkVersion failed" }

Write-Host "== Versions =="
# emcc needs the emsdk environment (python paths); source it in a child cmd so
# nothing leaks into this shell or the user's environment.
$EmsdkEnv = Join-Path $EmsdkDir 'emsdk_env.bat'
cmd /s /c "call `"$EmsdkEnv`" >nul 2>&1 && emcc --version"
if ($LASTEXITCODE -ne 0) { throw "emcc verification failed" }
# The bundled node is what runs the test suite (and what the wasm-core preset
# pins as CMAKE_CROSSCOMPILING_EMULATOR); report its version, not the system one.
$NodeExe = Get-ChildItem (Join-Path $EmsdkDir 'node\*\bin\node.exe') | Select-Object -First 1
if (-not $NodeExe) { throw "bundled node was not found under .deps\emsdk\node" }
Write-Host "bundled node $((& $NodeExe.FullName --version))"
Write-Host "== wasm toolchain setup complete =="

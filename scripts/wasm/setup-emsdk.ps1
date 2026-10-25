# One-time (idempotent) provisioning of the Emscripten toolchain for Patchy
# WebAssembly builds. Installs the pinned emsdk into .deps/emsdk (already
# gitignored, same layout idea as .deps/Qt) and activates it locally: activation
# only writes emsdk's own config files, never the user or system environment.
#
# 4.0.7 is the Emscripten version the Qt 6.10/6.11 documentation lists as
# supported for Qt for WebAssembly (doc.qt.io/qt-6/wasm.html and the Qt 6.10
# Tools and Versions wiki), so both wasm presets share one toolchain.
#
# Run from anywhere:
#   pwsh -File scripts\wasm\setup-emsdk.ps1
# Rerunning is a fast no-op once everything is installed. Pass -EmsdkVersion to
# provision a different release (versions install side by side; activate picks).
param(
  [string]$EmsdkVersion = '4.0.7'
)
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$EmsdkDir = Join-Path $RepoRoot '.deps\emsdk'
$EmsdkBat = Join-Path $EmsdkDir 'emsdk.bat'

Write-Host "== Patchy wasm toolchain setup (emsdk $EmsdkVersion) =="

if (-not (Test-Path $EmsdkBat)) {
  Write-Host "== Cloning emsdk into $EmsdkDir =="
  git clone https://github.com/emscripten-core/emsdk.git $EmsdkDir
  if ($LASTEXITCODE -ne 0) { throw "git clone of emsdk failed" }
} else {
  # An existing clone only knows the releases listed at its checkout; refresh it
  # so a newer pinned version is installable ("unknown version" otherwise).
  Write-Host "== Refreshing the emsdk clone =="
  git -C $EmsdkDir fetch --tags origin
  if ($LASTEXITCODE -ne 0) { throw "git fetch of emsdk failed" }
  git -C $EmsdkDir pull --ff-only
  if ($LASTEXITCODE -ne 0) { throw "git pull of emsdk failed" }
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
# The bundled node is what runs the test suite and the local servers. Resolve it
# the way every wasm script does (find-node.ps1 reads the activated NODE_JS from
# .emscripten; docs/wasm.md "Node resolution") rather than trusting a directory
# listing: an alternate emsdk release leaves a second node directory behind.
$EmscriptenConfig = Join-Path $EmsdkDir '.emscripten'
$NodeLine = (Get-Content $EmscriptenConfig | Where-Object { $_ -match '^NODE_JS' } | Select-Object -First 1)
Write-Host "activated node config: $NodeLine"
$NodeExe = & (Join-Path $PSScriptRoot 'find-node.ps1') -RepoRoot $RepoRoot
Write-Host "bundled node $((& $NodeExe --version)) at $NodeExe"

# ctest cannot call a script, so the wasm-core preset pins the same node as a
# literal CMAKE_CROSSCOMPILING_EMULATOR path. A new emsdk release that bundles
# another node (4.0.7 moved from 22.16.0 under bin\ to 24.19.0 at the directory
# root) would otherwise leave ctest pointing at a file no fresh machine has.
$Presets = Get-Content (Join-Path $RepoRoot 'CMakePresets.json') -Raw | ConvertFrom-Json
$WasmCore = $Presets.configurePresets | Where-Object { $_.name -eq 'wasm-core' } | Select-Object -First 1
if (-not $WasmCore) { throw "CMakePresets.json has no wasm-core configure preset" }
$Emulator = $WasmCore.cacheVariables.CMAKE_CROSSCOMPILING_EMULATOR
if (-not $Emulator) { throw "the wasm-core preset sets no CMAKE_CROSSCOMPILING_EMULATOR" }
$EmulatorPath = [IO.Path]::GetFullPath($Emulator.Replace('${sourceDir}', $RepoRoot))
if ($EmulatorPath -ne [IO.Path]::GetFullPath($NodeExe)) {
  throw ("CMakePresets.json wasm-core pins CMAKE_CROSSCOMPILING_EMULATOR to `"$Emulator`", but emsdk " +
    "$EmsdkVersion activated `"$NodeExe`". Point the preset at the activated node (keep the " +
    '${sourceDir} prefix) and update the node version in docs/wasm.md "Node resolution".')
}
Write-Host "wasm-core preset emulator matches the activated node"
Write-Host "== wasm toolchain setup complete =="

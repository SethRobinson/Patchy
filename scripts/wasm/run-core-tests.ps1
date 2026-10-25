# Runs the WebAssembly build of patchy_core_tests under the emsdk-bundled node.
# Usage:
#   pwsh -File scripts\wasm\run-core-tests.ps1 [name-substring-filter]
# Runs from build\wasm-core, mirroring the native convention of running test
# binaries from their build directory, so the relative test-artifacts/ output
# lands in build\wasm-core\test-artifacts.
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BuildDir = Join-Path $RepoRoot 'build\wasm-core'
$TestJs = Join-Path $BuildDir 'patchy_core_tests.js'
if (-not (Test-Path $TestJs)) {
  throw "patchy_core_tests.js was not found; build the wasm-core preset first (see docs/wasm.md)"
}
# find-node.ps1 is the shared resolution rule (docs/wasm.md "Node resolution");
# it throws when no emsdk node is installed.
$NodeExe = & (Join-Path $PSScriptRoot 'find-node.ps1') -RepoRoot $RepoRoot

Push-Location $BuildDir
try {
  & $NodeExe $TestJs @args
  exit $LASTEXITCODE
}
finally {
  Pop-Location
}

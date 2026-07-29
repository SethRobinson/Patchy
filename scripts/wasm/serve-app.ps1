# Serves the wasm-release build directory on localhost for browser testing.
# Usage:
#   pwsh -File scripts\wasm\serve-app.ps1 [port]
# Default port 8973; open http://localhost:<port>/patchy.html
$ErrorActionPreference = 'Stop'

$Port = if ($args.Count -ge 1) { [int]$args[0] } else { 8973 }
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BuildDir = Join-Path $RepoRoot 'build\wasm-release'
if (-not (Test-Path (Join-Path $BuildDir 'patchy.html'))) {
  throw "patchy.html was not found; build the wasm-release preset first (see docs/wasm.md)"
}
$NodeExe = Get-ChildItem (Join-Path $RepoRoot '.deps\emsdk\node\*\bin\node.exe') | Select-Object -First 1
if (-not $NodeExe) { throw "The emsdk-bundled node was not found; run scripts\wasm\setup-emsdk.ps1 first" }

& $NodeExe.FullName (Join-Path $PSScriptRoot 'serve.mjs') $BuildDir $Port

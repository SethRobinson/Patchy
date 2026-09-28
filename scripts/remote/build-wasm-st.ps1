<#
Remote half of scripts\remote\build-wasm-st-remote.ps1. Runs on the Windows offload host
inside its work tree right after the snapshot checkout: configure and build the
wasm-release-st preset (the single-threaded Qt for WebAssembly app served to Safari) in
the emsdk plus VS developer environment, throttled to below-normal priority. It never
stages or precompresses anything: the caller copies the four output files back and
build-wasm.bat stages them beside the locally built multithreaded variant.

  build-wasm-st.ps1 [-Jobs 8]

-Jobs 8 keeps a 12-thread / 16 GB streaming PC usable while it builds (setup notes in
agents_local.md). Prints the output files with sizes on success; nonzero exit otherwise.
#>
param([int]$Jobs = 8)

$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $repo

foreach ($required in @(
    '.deps\emsdk\emsdk_env.bat',
    '.deps\Qt\6.10.3\wasm_singlethread\lib\cmake\Qt6\qt.toolchain.cmake',
    '.deps\Qt\6.10.3\msvc2022_64\bin\qmake.exe')) {
  if (-not (Test-Path (Join-Path $repo $required))) {
    Write-Host "build-wasm-st.ps1: $required is missing on this host (see docs/wasm.md for the emsdk and Qt wasm kit setup)"
    exit 2
  }
}

Write-Host "== wasm-release-st on $env:COMPUTERNAME at $(git rev-parse --short HEAD) =="
# cmd is the only shell that can `call` the two environment batch files; a Windows OpenSSH
# session gives Windows PowerShell 5.1, so no && outside the cmd string.
$envSetup = 'call .deps\emsdk\emsdk_env.bat >nul 2>&1 && call scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul'

Write-Host '== configure (wasm-release-st) =='
cmd /s /c "$envSetup && cmake --preset wasm-release-st"
if ($LASTEXITCODE -ne 0) { Write-Host "configure failed ($LASTEXITCODE)"; exit 1 }

Write-Host "== build (wasm-release-st, -j $Jobs, below-normal priority) =="
cmd /s /c "$envSetup && scripts\run-throttled.bat cmake --build --preset wasm-release-st -j $Jobs"
if ($LASTEXITCODE -ne 0) { Write-Host "build failed ($LASTEXITCODE)"; exit 1 }

$outputs = @('patchy.js', 'patchy.wasm', 'patchy.data', 'qtloader.js') | ForEach-Object { Join-Path $repo "build\wasm-release-st\$_" }
$missing = $outputs | Where-Object { -not (Test-Path $_) }
if ($missing) { Write-Host "build finished but these outputs are missing: $($missing -join ', ')"; exit 1 }
Get-Item $outputs | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize | Out-String | Write-Host
Write-Host 'WASM_ST_BUILD_DONE'
exit 0

<#
Remote half of scripts\remote\remote-build.ps1 -Target wasm, run in a Windows build host's
work tree right after the snapshot checkout. Builds and stages the web site
(scripts\release\build-wasm.bat, into build\package\wasm-site), then builds the wasm-core
preset and runs the full wasm core suite under the emsdk-bundled node.

  wasm-build-and-test.ps1 [-Jobs 8] [-SkipTests]

The host needs what build-wasm.bat needs locally: emsdk and the Qt wasm_multithread kit in
.deps (scripts\wasm\setup-emsdk.ps1, setup-qt-wasm.ps1). The pinned onnxruntime-web and
models are fetched here by setup-ai-deps.ps1 -Web (a no-op once they are in place).
Everything heavy goes through scripts\run-throttled.bat (below-normal priority, real exit
codes, the machine-wide build lock), like build-and-test.ps1.

The suite's output goes to build\wasm-core\wasm-core-tests.log; the run ends with one
summary line (exit code, pass, fail and skip counts) and every [FAIL] line.
#>
param(
  [int]$Jobs = 8,
  [switch]$SkipTests
)

$ErrorActionPreference = 'Continue'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $repo
$throttled = Join-Path $repo 'scripts\run-throttled.bat'

Write-Host '== setup-ai-deps -Web =='
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\setup-ai-deps.ps1 -Web
if ($LASTEXITCODE -ne 0) { Write-Host "setup-ai-deps.ps1 -Web failed ($LASTEXITCODE)"; exit 1 }

Write-Host "== build-wasm.bat (wasm-release site, -j $Jobs, below-normal priority) =="
$env:NO_PAUSE = '1'
$env:CMAKE_BUILD_PARALLEL_LEVEL = "$Jobs"
$start = Get-Date
& $throttled cmd /c scripts\release\build-wasm.bat
$siteExit = $LASTEXITCODE
Write-Host ("== build-wasm.bat exit $siteExit after {0:N0} s ==" -f ((Get-Date) - $start).TotalSeconds)
if ($siteExit -ne 0) { exit $siteExit }
if (-not (Test-Path (Join-Path $repo 'build\package\wasm-site\patchy.html'))) {
  Write-Host 'build-wasm.bat reported success but build\package\wasm-site\patchy.html is missing'
  exit 1
}

if ($SkipTests) {
  Write-Host '== tests skipped =='
  exit 0
}

$wasmEnv = 'call .deps\emsdk\emsdk_env.bat >nul 2>&1 && call scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul'
Write-Host '== configure wasm-core =='
cmd /s /c "$wasmEnv && cmake --preset wasm-core 2>&1"
if ($LASTEXITCODE -ne 0) { Write-Host "wasm-core configure failed ($LASTEXITCODE)"; exit 1 }
Write-Host "== build wasm-core (-j $Jobs, below-normal priority) =="
cmd /s /c "$wasmEnv && scripts\run-throttled.bat cmake --build --preset wasm-core -j $Jobs 2>&1"
if ($LASTEXITCODE -ne 0) { Write-Host "wasm-core build failed ($LASTEXITCODE)"; exit 1 }

# run-core-tests.ps1 resolves the emsdk node (docs/wasm.md, "Node resolution") and runs from
# build\wasm-core. ctest is not used: its pinned node path is the dev box's.
$log = Join-Path $repo 'build\wasm-core\wasm-core-tests.log'
Write-Host "== wasm core suite (log $log) =="
cmd /s /c "scripts\run-throttled.bat powershell -NoProfile -ExecutionPolicy Bypass -File scripts\wasm\run-core-tests.ps1 > `"$log`" 2>&1"
$testExit = $LASTEXITCODE
$lines = @(Get-Content $log -ErrorAction SilentlyContinue)
$pass = @($lines | Where-Object { $_ -match '^\[PASS\]' }).Count
$fail = @($lines | Where-Object { $_ -match '^\[FAIL\]' }).Count
$skip = @($lines | Where-Object { $_ -match '^\[SKIP\]' }).Count
$lines | Where-Object { $_ -match '^\[FAIL\]|^\[SUMMARY\]' } | ForEach-Object { Write-Host $_ }
Write-Host "== wasm core suite: exit $testExit, $pass passed, $fail failed, $skip skipped =="
if ($testExit -eq 0 -and $fail -gt 0) { $testExit = 1 }
exit $testExit

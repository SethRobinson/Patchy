<#
Release verification on this Windows machine (docs/release-process.md, "Build and upload
order"): both full native suites from build\release, then the MCP client suite against the
built connector and against the staged package's connector.

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\verify-windows.ps1 [-SkipCore] [-SkipUi]

Run it after the windows builder has finished (release-all-automated.bat verify-windows does
this unattended), never beside a build or another suite (docs/testing.md, "Running and
filtering", names the state UI runs share). Each step
logs to build\release-logs\verify-windows-<step>.log and writes exit=<code> beside it; a
step that exits 0 but printed [FAIL] counts as failed. The core and ui steps also judge their
skips (skip-check.ps1, docs/testing-skips.md): they print each [SUMMARY] line and the skip
count, and a skipped test missing from local-test-fixtures\expected-skips\<computer>-<suite>.txt
fails the step; with no list they print the skips and write a candidate list beside the log.
The first failing step ends the run.
-SkipCore / -SkipUi are for a re-verification after a change those suites already covered.
#>
param(
  [switch]$SkipCore,
  [switch]$SkipUi
)
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'release-process-run.ps1')
. (Join-Path $PSScriptRoot 'skip-check.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
if (-not (Test-Path 'CMakeLists.txt')) { throw "not the repository root: $root" }
$logs = Join-Path $root 'build\release-logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null

$python = Join-Path $root '.deps\mcp-client\Scripts\python.exe'
$required = @('build\release\patchy_core_tests.exe', 'build\release\patchy_ui_visual_tests.exe',
              'build\release\patchy-mcp.exe', 'build\package\staging\Patchy\patchy-mcp.exe', $python)
foreach ($path in $required) {
  if (-not (Test-Path $path)) { Write-Output "missing $path (run the windows builder first)"; exit 1 }
}

$steps = @()
if (-not $SkipCore) {
  $steps += @{ Name = 'core'; Suite = 'patchy_core_tests'; Command = 'cd /d build\release && ..\..\scripts\run-throttled.bat .\patchy_core_tests.exe' }
}
if (-not $SkipUi) {
  $steps += @{ Name = 'ui'; Suite = 'patchy_ui_visual_tests'; Command = 'cd /d build\release && ..\..\scripts\run-throttled.bat .\patchy_ui_visual_tests.exe' }
}
$steps += @{ Name = 'mcp'; Command = "scripts\run-throttled.bat `"$python`" tests\mcp_client_tests.py build\release\patchy-mcp.exe" }
$steps += @{ Name = 'package-mcp'; Command = "scripts\run-throttled.bat `"$python`" tests\mcp_client_tests.py build\package\staging\Patchy\patchy-mcp.exe" }

$env:QT_QPA_PLATFORM = 'offscreen'
Remove-Item Env:PATCHY_UI_TEST_FILTER -ErrorAction SilentlyContinue
foreach ($step in $steps) {
  $log = Join-Path $logs ('verify-windows-' + $step.Name + '.log')
  $marker = [IO.Path]::ChangeExtension($log, '.exit')
  if (Test-Path $marker) { [IO.File]::Delete($marker) }
  Write-Output ('== ' + $step.Name + ' ==')
  $code = Invoke-CmdLine ($step.Command + ' > "' + $log + '" 2>&1')
  $failures = @(Select-String -LiteralPath $log -SimpleMatch '[FAIL]')
  if ($code -eq 0 -and $failures.Count -gt 0) { $code = 1 }
  $passes = @(Select-String -LiteralPath $log -SimpleMatch '[PASS]').Count
  $skips = @(Select-String -LiteralPath $log -SimpleMatch '[SKIP]').Count
  Write-Output ("{0}: exit={1} pass={2} fail={3} skip={4}" -f $step.Name, $code, $passes, $failures.Count, $skips)
  $failures | Select-Object -First 10 | ForEach-Object { $_.Line }
  if ($step.Suite) {
    # A skipped test checked nothing: a lost fixture folder must not read as a pass.
    $unexpected = 0
    Test-PatchySkips -Log $log -HostName $env:COMPUTERNAME -Root $root -DefaultSuite $step.Suite -Unexpected ([ref]$unexpected)
    if ($unexpected -gt 0) {
      Write-Output ("{0}: {1} unexpected skip(s), named above" -f $step.Name, $unexpected)
      if ($code -eq 0) { $code = 1 }
    }
  }
  [IO.File]::WriteAllText($marker, "exit=$code`r`n")
  if ($code -ne 0) { exit $code }
}
Write-Output '== ALL WINDOWS RELEASE CHECKS PASSED =='
exit 0

<#
Dot-sourced by verify-windows.ps1 and verify-remote.ps1 (docs/testing-skips.md). Judges the
[SKIP] lines of a suite log against the untracked expected-skip list for that host and suite:

  <local fixtures>\expected-skips\<host>-<suite>.txt

<local fixtures> is PATCHY_LOCAL_FIXTURES_DIR when set, else the repository's
local-test-fixtures. <suite> is the test binary's name (patchy_core_tests,
patchy_ui_visual_tests). The list holds one test name per line; text after the first tab or
space is ignored, and so are blank lines and lines starting with #. A suite's own
test-artifacts\skipped-<suite>.txt (name, tab, reason) is therefore a valid list as it stands.

Test-PatchySkips writes its report to the output and stores the number of unexpected skips
in -Unexpected (a [ref]): a skip whose test is not listed fails the step. A listed test that
ran is reported, not failed (the list is stale). A missing list prints the skips, writes them
as a candidate list beside the log, and says how to install it; it never fails the step.
<host> is the ssh host's name up to its first dot, or the computer name, in lower case.
#>

function Get-PatchySkipHostName([string]$name) {
  # user@mac-build-host.local -> mac-build-host; WINDOWS-PC -> windows-pc.
  $hostPart = ($name -split '@')[-1]
  $hostPart = ($hostPart -split '\.')[0]
  return ($hostPart -replace '[^A-Za-z0-9_-]', '_').ToLowerInvariant()
}

function Get-PatchyExpectedSkipDir([string]$root) {
  if ($env:PATCHY_LOCAL_FIXTURES_DIR) { return Join-Path $env:PATCHY_LOCAL_FIXTURES_DIR 'expected-skips' }
  return Join-Path $root 'local-test-fixtures\expected-skips'
}

# Splits a log into suites. $defaultSuite names the suite of lines before any
# "== patchy_<name>_tests" header (a log of one suite has none).
function Read-PatchySuiteLog([string]$log, [string]$defaultSuite) {
  $suites = [ordered]@{}
  $current = $defaultSuite
  foreach ($line in [IO.File]::ReadAllLines($log)) {
    if ($line -match '^== (patchy_[A-Za-z0-9_]+_tests)\b') {
      $current = $Matches[1]
      continue
    }
    $isSkip = $line -match '\[SKIP\] ([^:\s]+): (.*)$'
    $isSummary = $line -match '(\[SUMMARY\] .*)$'
    if (-not ($isSkip -or $isSummary) -or -not $current) { continue }
    if (-not $suites.Contains($current)) {
      $suites[$current] = [pscustomobject]@{ Skips = [ordered]@{}; Summaries = New-Object System.Collections.Generic.List[string] }
    }
    if ($line -match '\[SKIP\] ([^:\s]+): (.*)$') {
      $suites[$current].Skips[$Matches[1]] = $Matches[2]
    } elseif ($line -match '(\[SUMMARY\] .*)$') {
      $suites[$current].Summaries.Add($Matches[1])
    }
  }
  return $suites
}

function Test-PatchySkips {
  param(
    [Parameter(Mandatory = $true)][string]$Log,
    [Parameter(Mandatory = $true)][string]$HostName,
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ref]$Unexpected,
    [string]$DefaultSuite = ''
  )
  $Unexpected.Value = 0
  if (-not (Test-Path -LiteralPath $Log)) { return }
  $hostKey = Get-PatchySkipHostName $HostName
  $listDir = Get-PatchyExpectedSkipDir $Root
  $suites = Read-PatchySuiteLog $Log $DefaultSuite
  foreach ($suite in $suites.Keys) {
    $entry = $suites[$suite]
    foreach ($summary in $entry.Summaries) { Write-Output ("{0}: {1}" -f $suite, $summary) }
    $names = @($entry.Skips.Keys | Sort-Object)
    Write-Output ("{0}: {1} skipped" -f $suite, $names.Count)
    $list = Join-Path $listDir ("$hostKey-$suite.txt")
    if (-not (Test-Path -LiteralPath $list)) {
      $candidate = Join-Path (Split-Path -Parent $Log) ("expected-skips-$hostKey-$suite.txt")
      $lines = @($names | ForEach-Object { "$_`t" + $entry.Skips[$_] })
      [IO.File]::WriteAllLines($candidate, [string[]]$lines)
      Write-Output ("{0}: no expected-skip list at {1}; the skips are not judged. Skipped tests:" -f $suite, $list)
      $lines | ForEach-Object { Write-Output "  $_" }
      Write-Output ("{0}: once each skip above is a known absent input, install the list: copy `"{1}`" `"{2}`"" -f $suite, $candidate, $list)
      continue
    }
    $expected = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($raw in [IO.File]::ReadAllLines($list)) {
      $text = $raw.Trim()
      if (-not $text -or $text.StartsWith('#')) { continue }
      [void]$expected.Add(($text -split '\s', 2)[0])
    }
    $unlisted = @($names | Where-Object { -not $expected.Contains($_) })
    $nowRunning = @($expected | Where-Object { -not $entry.Skips.Contains($_) } | Sort-Object)
    foreach ($name in $unlisted) {
      Write-Output ("[FAIL] unexpected skip in {0}: {1}: {2} (not in {3}; restore the input, or add the name if the skip is now expected)" -f $suite, $name, $entry.Skips[$name], $list)
    }
    if ($entry.Summaries.Count -gt 0 -or $names.Count -gt 0) {
      foreach ($name in $nowRunning) {
        Write-Output ("{0}: {1} is in the expected-skip list but did not skip (remove it from {2})" -f $suite, $name, $list)
      }
    }
    $Unexpected.Value += $unlisted.Count
  }
}

<#
The warning check for one platform, on its build host (docs/release-process.md, "Before the
version bump"; the check itself: docs/platform.md, "Warning verification"):

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\warncheck.ps1 -Target windows|windows-arm64|mac|linux|wasm [-Clean]

Each run is scripts\remote\remote-build.ps1 -Warncheck: the preset's default target in its
own build\warncheck-<preset> on the host, failing on any warning line, no tests.

  windows        opt-dev on the first idle offload host (MSVC; the same compiler warnings
                 as the release preset, without its LTCG link)
  windows-arm64  windows-arm64-opt-dev on the ARM64 host
  mac            mac-release (Apple clang)
  linux          linux-flatpak-qt (the Flatpak SDK's GCC and Qt), then linux-appimage (gcc-12
                 and Qt 6.8.3 in the AppImage's container)
  wasm           wasm-release (the web app) and wasm-core on the wasm host (Emscripten clang)

The first run on a host builds everything; later runs compile only what changed since the
last pass. -Clean starts every run from an empty folder. Every run of the target runs even
when an earlier one fails, so one pass reports every warning. Exit 0 only when all passed;
75 when a host was busy or locked and nothing else failed. The release-all-automated.bat
targets warncheck-<target> (and warncheck, all five) run this unattended.
#>
param(
  [Parameter(Mandatory = $true)][ValidateSet('windows', 'windows-arm64', 'mac', 'linux', 'wasm')][string]$Target,
  [switch]$Clean
)
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $root
if (-not (Test-Path 'CMakeLists.txt')) { throw "not the repository root: $root" }

$runs = switch ($Target) {
  'windows' { , @{ Target = 'windows'; Preset = 'opt-dev' } }
  'windows-arm64' { , @{ Target = 'windows-arm64'; Preset = 'windows-arm64-opt-dev' } }
  'mac' { , @{ Target = 'mac'; Preset = 'mac-release' } }
  'linux' { @{ Target = 'linux'; Preset = 'linux-flatpak-qt' }, @{ Target = 'linux'; Preset = 'linux-appimage' } }
  'wasm' { , @{ Target = 'wasm'; Preset = '' } }
}

$results = @()
foreach ($run in $runs) {
  $name = if ($run.Preset) { "$($run.Target) $($run.Preset)" } else { "$($run.Target) wasm-release, wasm-core" }
  $start = Get-Date
  Write-Output "== warncheck $name started $($start.ToString('yyyy-MM-dd HH:mm:ss')) =="
  $buildArgs = @{ Target = $run.Target; Warncheck = $true }
  if ($run.Preset) { $buildArgs.Preset = $run.Preset }
  if ($Clean) { $buildArgs.Clean = $true }
  # remote-build.ps1 throws on a setup problem (no reachable host, a bad config).
  try {
    & (Join-Path $root 'scripts\remote\remote-build.ps1') @buildArgs
    $code = $LASTEXITCODE
  } catch {
    Write-Output "remote-build.ps1 stopped: $_"
    $code = 1
  }
  $minutes = [int][Math]::Ceiling(((Get-Date) - $start).TotalMinutes)
  $state = switch ($code) { 0 { 'PASSED' } 75 { 'NOT RUN (host busy or locked)' } default { "FAILED (exit $code)" } }
  $results += [pscustomobject]@{ Name = $name; Code = $code; Line = "  $name`: $state, $minutes min" }
}

Write-Output "== warncheck $Target summary =="
$results | ForEach-Object { Write-Output $_.Line }
$failed = @($results | Where-Object { $_.Code -ne 0 -and $_.Code -ne 75 })
$busy = @($results | Where-Object { $_.Code -eq 75 })
if ($failed.Count) { Write-Output "== WARNCHECK $Target FAILED =="; exit 1 }
if ($busy.Count) { Write-Output "== WARNCHECK $Target INCOMPLETE: a host was busy =="; exit 75 }
Write-Output "== WARNCHECK $Target PASSED =="
exit 0

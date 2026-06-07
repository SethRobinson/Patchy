param(
  [string]$DestinationDir,
  [switch]$Force
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
  $DestinationDir = Join-Path $repo "local-test-fixtures/psd"
}

$fixtures = @(
  @{ Name = "Template.psd"; Source = "D:/projects/proton_svn/RTOpenCV/Template.psd" },
  @{ Name = "checkbox.psd"; Source = "D:/projects/proton_svn/RTPeople/media/interface/checkbox.psd" },
  @{ Name = "ipad_main_v04.psd"; Source = "D:/projects/proton/RTDink/media/interface/ipad/ipad_main_v04.psd" },
  @{ Name = "Horror VirtualBoy.psd"; Source = "D:/projects/C2/MiscPrints/Horror VirtualBoy.psd" },
  @{ Name = "Arduboy.psd"; Source = "D:/projects/C2/MiscPrints/Arduboy.psd" },
  @{ Name = "Title Screen_demo.psd"; Source = "D:/projects/DungeonScroll/media/Demo/Title Screen_demo.psd" },
  @{ Name = "Duke nukem mobile.psd"; Source = "C:/temp/Duke nukem mobile.psd" },
  @{ Name = "CDi_A4.psd"; Source = "D:/projects/C2/ExhibitSigns/CDi_A4.psd" },
  @{ Name = "tips.psd"; Source = "D:/projects/proton/RTDink/media/interface/win/tips.psd" },
  @{
    Name = "C2Kyoto Nintendo NES Cartridge Label Template (Front).psd"
    Source = "D:/projects/cc65/c2game/screenshots_and_labels/C2Kyoto Nintendo NES Cartridge Label Template (Front).psd"
  }
)

New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null

$copied = 0
$skipped = 0
$missing = 0

foreach ($fixture in $fixtures) {
  $source = Get-Item -LiteralPath $fixture.Source -ErrorAction SilentlyContinue
  $destination = Join-Path $DestinationDir $fixture.Name

  if ($null -eq $source) {
    Write-Warning "Missing source PSD: $($fixture.Source)"
    $missing++
    continue
  }

  $destinationItem = Get-Item -LiteralPath $destination -ErrorAction SilentlyContinue
  $needsCopy = $Force -or $null -eq $destinationItem
  if (!$needsCopy) {
    $timeDelta = [math]::Abs(($destinationItem.LastWriteTimeUtc - $source.LastWriteTimeUtc).TotalSeconds)
    $needsCopy = $destinationItem.Length -ne $source.Length -or $timeDelta -gt 2.0
  }

  if ($needsCopy) {
    Copy-Item -LiteralPath $source.FullName -Destination $destination -Force
    $copiedItem = Get-Item -LiteralPath $destination
    $copiedItem.LastWriteTimeUtc = $source.LastWriteTimeUtc
    Write-Host "Copied $($fixture.Name)"
    $copied++
  } else {
    Write-Host "Up to date $($fixture.Name)"
    $skipped++
  }
}

Write-Host "Local PSD fixture sync complete: copied=$copied skipped=$skipped missing=$missing destination=$DestinationDir"
if ($missing -gt 0) {
  exit 1
}

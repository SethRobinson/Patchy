# Fetches the Filter Foundry test plug-ins (docs/plugins.md) into the untracked local fixture
# tree, local-test-fixtures/photoshop-plugins/filter-foundry. The four standalone .8bf files
# are GPL binaries the repository does not carry; each download is verified against the
# SHA-256 recorded here and in test-fixtures/photoshop-plugins/README.md, and the author's
# .afs expression files come along as their source. Tests that need the plug-ins skip while
# the folder is missing.
#
#   powershell -ExecutionPolicy Bypass -File scripts\fetch-test-plugins.ps1 [-DestinationDir <dir>]
param(
  [string]$DestinationDir
)

$ErrorActionPreference = 'Stop'

$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
if ([string]::IsNullOrWhiteSpace($DestinationDir)) {
  $DestinationDir = Join-Path $repo 'local-test-fixtures\photoshop-plugins\filter-foundry'
}
New-Item -ItemType Directory -Force $DestinationDir | Out-Null

$baseUrl = 'https://misc.daniel-marschall.de/photoshop_filters/'
$files = @(
  @{ Name = 'Greyscale.8bf'; Sha256 = 'ab0533c931ddb49093f09b5f80cc5e02b007c36b311572db19bb780a0a04fd6d' },
  @{ Name = 'Greyscale64.8bf'; Sha256 = '301a1c7766667c3bff4d83b123341f58c95acc16ce504b9b8f5e47098149e6e0' },
  @{ Name = 'White to Transparent.8bf'; Sha256 = 'f5bf415279d3a645bcd32848bf7b07d05e247877e4ad46e6b3d0602b218f6625' },
  @{ Name = 'White to Transparent64.8bf'; Sha256 = '263e4b30f2ac1934fbd1c04572bbbd4120b9201a295281d9100b76ae03a8fa56' },
  @{ Name = 'Greyscale.afs'; Sha256 = 'f08d9829f9bcaef1214390fc6ad20f87b6d0953b6cd3252b5090350474d97b35' },
  @{ Name = 'White to Transparent.afs'; Sha256 = '56b6d8c9eb4064d319e656cbd9aef6d11cbfa8e052090ed4a729540dff6c87dc' }
)

function Get-Sha256([string]$Path) {
  (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

# Downloads to a .part sibling, verifies, then renames, so an interrupted run
# never leaves a file that a later run would mistake for a verified one.
function Get-VerifiedFile([string]$Url, [string]$Destination, [string]$Sha256) {
  if (Test-Path -LiteralPath $Destination) {
    if ((Get-Sha256 $Destination) -eq $Sha256) { return }
    Write-Host "checksum mismatch, downloading again: $Destination"
    Remove-Item -LiteralPath $Destination
  }
  $Part = "$Destination.part"
  Write-Host "downloading $Url"
  Invoke-WebRequest -Uri $Url -OutFile $Part -UseBasicParsing
  $Actual = Get-Sha256 $Part
  if ($Actual -ne $Sha256) {
    Remove-Item -LiteralPath $Part
    throw "SHA-256 mismatch for $Url (expected $Sha256, got $Actual)"
  }
  Move-Item -LiteralPath $Part -Destination $Destination
}

foreach ($file in $files) {
  $url = $baseUrl + [uri]::EscapeDataString($file.Name)
  Get-VerifiedFile $url (Join-Path $DestinationDir $file.Name) $file.Sha256
}
Write-Host "Filter Foundry test plug-ins ready in $DestinationDir"

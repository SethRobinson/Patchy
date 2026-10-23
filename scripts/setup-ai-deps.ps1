# One-time (idempotent) provisioning of the automatic-selection dependencies:
# the pinned ONNX Runtime CPU package and the pinned segmentation models, plus, with
# -Web, the pinned onnxruntime-web package the wasm app stages beside patchy.html.
# Everything lands in .deps (gitignored, same idea as .deps/Qt); nothing here is
# ever committed. Every download is checked against the SHA-256 pinned below, and
# a file that fails the check is deleted, never used. See docs/ai-selection.md.
#
# Run from anywhere (a git worktree provisions the main checkout's .deps, which is
# where CMake looks for it):
#   pwsh -File scripts\setup-ai-deps.ps1
#   pwsh -File scripts\setup-ai-deps.ps1 -Web     # also onnxruntime-web (wasm builds)
# Rerunning is a fast no-op once everything is present and verified.
param(
  [string]$DepsDir = '',
  [ValidateSet('win-x64', 'win-arm64')]
  [string]$Platform = 'win-x64',
  [switch]$Web
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Keep in sync with scripts/setup-ai-deps.sh and cmake/patchy_ai.cmake.
$OrtVersion = '1.30.0'
$OrtPackages = @{
  'win-x64'   = 'c6ba983baf5681af108599675d2a89c2d145512d02de28aed0bff177cd0ba949'
  'win-arm64' = 'e53db8a50b23ae35be901cc93428baf997dc8d420333b097b2eae53d3ea9f2d3'
}
# onnxruntime-web: the npm package (its tarball's SHA-256; npm's own sha512 integrity is
# q0y+JrrtukXSzsBWEMccVfqX25LRmosXHF+CaRJmg8pZClzcV7svNc4rKY3jL02Vb7QmRMDs1SigqR4CXAfKYQ==)
# and the license texts at the same release tag, which the npm package does not carry.
$OrtWebVersion = '1.30.0'
$OrtWebPackageSha256 = 'd2228df7e4616bc3348bf504ee888f3bec43789a273f0a63f3e68d203ce3bf71'
$OrtWebFiles = @('ort.wasm.min.mjs', 'ort-wasm-simd-threaded.mjs', 'ort-wasm-simd-threaded.wasm')
$OrtWebLicenses = @{
  'LICENSE'               = '2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c'
  'ThirdPartyNotices.txt' = '143764b952fdb1a7c69ce653bfba74a7744d6a8a573bfb73e235fba356c83de3'
}
# The models Patchy ships: converted from the pinned upstream fp32 files by
# scripts/ai/quantize_models.py (byte-reproducible) and hosted on the ai-models-N releases.
# MobileSAM is what builds bundle. EfficientViT-SAM (ai-models-2) is Object Selection's High
# Quality model: users download it in the app; dev trees fetch it so tests can stage it.
$Models = @(
  @{ Name = 'efficientvit_sam_xl0_encoder_fp16.onnx'
     Url = 'https://github.com/SethRobinson/Patchy/releases/download/ai-models-2/efficientvit_sam_xl0_encoder_fp16.onnx'
     Sha256 = 'a28cf6978672e202b597e948f71c09a31267ee947d26579f6ca290281e5bf101' },
  @{ Name = 'efficientvit_sam_xl0_decoder_multi_fp16.onnx'
     Url = 'https://github.com/SethRobinson/Patchy/releases/download/ai-models-2/efficientvit_sam_xl0_decoder_multi_fp16.onnx'
     Sha256 = '632613ec55ebd52a72f0a1933c297ce67b8563ff2e970b0c2eea059373ba496b' },
  # TrustMark watermark models (docs/watermarks.md): Adobe's fp16 ONNX files, MIT. Users download
  # them in the app; dev trees fetch them so the watermark tests can stage them.
  @{ Name = 'trustmark_decoder_Q.onnx'
     Url = 'https://cai-watermark.adobe.net/watermarking/trustmark-models/decoder_Q.onnx'
     Sha256 = 'ee3268f057c9dabef680e169302f5973d0589feea86189ed229a896cc3aa88df' },
  @{ Name = 'trustmark_decoder_P.onnx'
     Url = 'https://cai-watermark.adobe.net/watermarking/trustmark-models/decoder_P.onnx'
     Sha256 = 'be6d7c33f8a7b376f179e75f3f7c58ff816a9ac7bb6d37fd0a729a635f624c35' },
  @{ Name = 'trustmark_decoder_C.onnx'
     Url = 'https://cai-watermark.adobe.net/watermarking/trustmark-models/decoder_C.onnx'
     Sha256 = '3d92f4a278bcf87ac7dc40248c36ae664fe5fdedfd19ca265f7ca730b271100a' },
  @{ Name = 'trustmark_encoder_Q.onnx'
     Url = 'https://cai-watermark.adobe.net/watermarking/trustmark-models/encoder_Q.onnx'
     Sha256 = '19b3d1b25836130ffd78775a8f61539f993375d1823ef0e59ba5b8dffb4f892d' },
  @{ Name = 'mobile_sam_image_encoder_int8.onnx'
     Url = 'https://github.com/SethRobinson/Patchy/releases/download/ai-models-1/mobile_sam_image_encoder_int8.onnx'
     Sha256 = '6af3f249f7a5a6a8a3ef988b6814015e8c3a300977c135d1f9572ed2f2930cd7' },
  @{ Name = 'sam_mask_decoder_multi_int8.onnx'
     Url = 'https://github.com/SethRobinson/Patchy/releases/download/ai-models-1/sam_mask_decoder_multi_int8.onnx'
     Sha256 = 'aa1410bec8f050b6017fba29ca07f16317029a8f74a98e5998aeed46680c10a6' }
)
# Select Subject is held back pending patent review. Do not download BiRefNet;
# existing local copies are left alone and the default build never stages them.

if (-not $DepsDir) {
  $CommonDir = (git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir).Trim()
  if ($LASTEXITCODE -ne 0 -or -not $CommonDir) { throw 'git rev-parse failed; pass -DepsDir' }
  $MainCheckout = Split-Path -Parent $CommonDir
  if (-not (Test-Path (Join-Path $MainCheckout 'CMakeLists.txt'))) {
    throw "main checkout not found at $MainCheckout; pass -DepsDir"
  }
  $DepsDir = Join-Path $MainCheckout '.deps'
}
New-Item -ItemType Directory -Force $DepsDir | Out-Null

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

Write-Host "== ONNX Runtime $OrtVersion ($Platform) =="
$OrtRoot = Join-Path $DepsDir "onnxruntime\$OrtVersion"
$OrtDir = Join-Path $OrtRoot $Platform
if (-not (Test-Path (Join-Path $OrtDir 'include\onnxruntime_cxx_api.h'))) {
  New-Item -ItemType Directory -Force $OrtRoot | Out-Null
  $Archive = Join-Path $OrtRoot "onnxruntime-$Platform-$OrtVersion.zip"
  $Url = "https://github.com/microsoft/onnxruntime/releases/download/v$OrtVersion/onnxruntime-$Platform-$OrtVersion.zip"
  Get-VerifiedFile $Url $Archive $OrtPackages[$Platform]
  $Staging = Join-Path $OrtRoot "staging-$Platform"
  if (Test-Path -LiteralPath $Staging) { Remove-Item -LiteralPath "$DepsDir\onnxruntime\$OrtVersion\staging-$Platform" -Recurse -Force }
  Expand-Archive -LiteralPath $Archive -DestinationPath $Staging
  $Inner = Join-Path $Staging "onnxruntime-$Platform-$OrtVersion"
  if (Test-Path -LiteralPath $OrtDir) { Remove-Item -LiteralPath "$DepsDir\onnxruntime\$OrtVersion\$Platform" -Recurse -Force }
  Move-Item -LiteralPath $Inner -Destination $OrtDir
  Remove-Item -LiteralPath "$DepsDir\onnxruntime\$OrtVersion\staging-$Platform" -Recurse -Force
  Remove-Item -LiteralPath $Archive
}
Write-Host "ONNX Runtime ready at $OrtDir"

Write-Host '== Segmentation models =='
$ModelDir = Join-Path $DepsDir 'ai-models'
New-Item -ItemType Directory -Force $ModelDir | Out-Null
foreach ($Model in $Models) {
  Get-VerifiedFile $Model.Url (Join-Path $ModelDir $Model.Name) $Model.Sha256
}
Write-Host "models ready at $ModelDir"

if ($Web) {
  Write-Host "== onnxruntime-web $OrtWebVersion =="
  $WebDir = Join-Path $DepsDir "onnxruntime-web\$OrtWebVersion"
  $WebReady = $true
  foreach ($Name in $OrtWebFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $WebDir $Name))) { $WebReady = $false }
  }
  if (-not $WebReady) {
    New-Item -ItemType Directory -Force $WebDir | Out-Null
    $Archive = Join-Path $WebDir "onnxruntime-web-$OrtWebVersion.tgz"
    Get-VerifiedFile "https://registry.npmjs.org/onnxruntime-web/-/onnxruntime-web-$OrtWebVersion.tgz" $Archive $OrtWebPackageSha256
    $Staging = Join-Path $WebDir 'staging'
    if (Test-Path -LiteralPath $Staging) { Remove-Item -LiteralPath "$DepsDir\onnxruntime-web\$OrtWebVersion\staging" -Recurse -Force }
    New-Item -ItemType Directory -Force $Staging | Out-Null
    $Members = $OrtWebFiles | ForEach-Object { "package/dist/$_" }
    tar -xzf $Archive -C $Staging @Members
    if ($LASTEXITCODE -ne 0) { throw "could not extract $Archive" }
    foreach ($Name in $OrtWebFiles) {
      Move-Item -Force -LiteralPath (Join-Path $Staging "package\dist\$Name") -Destination (Join-Path $WebDir $Name)
    }
    Remove-Item -LiteralPath "$DepsDir\onnxruntime-web\$OrtWebVersion\staging" -Recurse -Force
    Remove-Item -LiteralPath $Archive
  }
  foreach ($Name in $OrtWebLicenses.Keys) {
    Get-VerifiedFile "https://raw.githubusercontent.com/microsoft/onnxruntime/v$OrtWebVersion/$Name" (Join-Path $WebDir $Name) $OrtWebLicenses[$Name]
  }
  Write-Host "onnxruntime-web ready at $WebDir"
}
Write-Host '== automatic-selection dependencies ready =='

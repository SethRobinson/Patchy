# Runs capture-tracking-just.jsx in Photoshop over COM (logged-on desktop session only) and
# writes its PSDs, manifest.jsonl and capture-log.txt into -OutDir (default: the main checkout's
# local-test-fixtures/psd/ps2026_text_tracking_just). Creates and closes only its own documents.
#
#   powershell -File scripts\dev\text-tracking\capture-tracking-just.ps1 [-OutDir <dir>] [-ProgId Photoshop.Application.200]
#     [-Jsx capture-box-baseline.jsx]   (the box first-baseline sweep: manifest-baseline.jsonl)
param(
  [string]$OutDir = '',
  [string]$Jsx = 'capture-tracking-just.jsx',
  [string]$ProgId = 'Photoshop.Application'
)
$ErrorActionPreference = 'Stop'
if (-not $OutDir) {
  $common = (git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir).Trim()
  $OutDir = Join-Path (Split-Path $common -Parent) 'local-test-fixtures\psd\ps2026_text_tracking_just'
}
New-Item -ItemType Directory -Force $OutDir | Out-Null
$out = ((Resolve-Path $OutDir).Path -replace '\\', '/').TrimEnd('/') + '/'
$jsx = 'var OUT = "' + $out + '";' + "`n" + (Get-Content -Raw (Join-Path $PSScriptRoot $Jsx))
$ps = New-Object -ComObject $ProgId
$ps.DoJavaScript($jsx)

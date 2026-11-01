<#
Packs the complete source of Patchy's own GEGL (docs/gegl-filters.md, "Sources") into
build\package\PatchyGeglSources.zip, which every release publishes next to its downloads.

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\make-gegl-sources.ps1

Every archive in scripts\dev\gegl\sources.lock (GEGL, babl, what the macOS bundle builds,
MSYS2's source packages for the Windows libraries), each checked against its SHA-256, plus
the scripts that built the bundles and the Flatpak manifest. Archives come from
.deps\gegl\sources (this checkout's, else the main checkout's) and are downloaded there when
missing. Windows PowerShell 5.1 and pwsh 7 both work. Exit code 0 only when the zip holds
every locked archive.
#>
param([string]$Output)
$ErrorActionPreference = 'Stop'

function Fail([string]$message) {
  Write-Host "GEGL SOURCES FAILED: $message"
  exit 1
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) { Fail "not a Patchy checkout: $repoRoot" }
if (-not $Output) { $Output = Join-Path $repoRoot 'build\package\PatchyGeglSources.zip' }
$lockPath = Join-Path $repoRoot 'scripts\dev\gegl\sources.lock'

# The archive cache: this checkout's .deps, else the main checkout's (a worktree has none).
$cache = Join-Path $repoRoot '.deps\gegl\sources'
if (-not (Test-Path -LiteralPath $cache)) {
  $common = (git -C $repoRoot rev-parse --path-format=absolute --git-common-dir 2>$null)
  if ($LASTEXITCODE -eq 0 -and $common) {
    $mainCache = Join-Path (Split-Path -Parent $common.Trim()) '.deps\gegl\sources'
    if (Test-Path -LiteralPath $mainCache) { $cache = $mainCache }
  }
}
New-Item -ItemType Directory -Force -Path $cache | Out-Null

function Get-Sha256Hex([string]$path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $stream = [System.IO.File]::OpenRead($path)
  try { $bytes = $sha.ComputeHash($stream) } finally { $stream.Dispose(); $sha.Dispose() }
  return ([System.BitConverter]::ToString($bytes) -replace '-', '').ToLowerInvariant()
}

[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$entries = @()
foreach ($line in Get-Content -LiteralPath $lockPath) {
  $text = $line.Trim()
  if (-not $text -or $text.StartsWith('#')) { continue }
  $fields = $text -split '\s+'
  if ($fields.Count -ne 3 -or $fields[1] -notmatch '^[0-9a-f]{64}$') { Fail "bad line in sources.lock: $text" }
  $path = Join-Path $cache $fields[0]
  if (-not (Test-Path -LiteralPath $path) -or (Get-Sha256Hex $path) -ne $fields[1]) {
    Write-Host "downloading $($fields[2])"
    Invoke-WebRequest -UseBasicParsing -Uri $fields[2] -OutFile $path
  }
  if ((Get-Sha256Hex $path) -ne $fields[1]) { Fail "$($fields[0]) does not match its SHA-256 in sources.lock" }
  $entries += [pscustomobject]@{ Name = $fields[0]; Path = $path; Sha = $fields[1]; Url = $fields[2] }
}
if ($entries.Count -eq 0) { Fail 'sources.lock lists nothing' }

$readme = @"
Source code of the GEGL libraries Patchy ships (Filter > GEGL Filters)

Patchy's packages carry GEGL and babl (GNU LGPL 3 or later; GEGL's gegl-common-gpl3 module
under the GNU GPL 3), with GLib, json-glib, libintl and libiconv (GNU LGPL 2.1 or later),
libffi, PCRE2 and zlib (permissive licenses). This archive is their complete corresponding
source, as the licenses require:

  sources/        every source archive the packages were built from (sources.lock lists
                  each with its SHA-256 and where it came from); the Windows libraries are
                  MSYS2's source packages, which hold the upstream tarball, MSYS2's patches
                  and its PKGBUILD
  build-scripts/  the scripts that built them (scripts/dev/gegl in Patchy's repository) and
                  Patchy's Flatpak manifest, whose babl and gegl modules build the Linux ones

GEGL is built without a few operations, removed from its build lists by
build-scripts/scripts/dev/gegl/common.sh before the build; nothing else is changed.
Patchy's own source, the GEGL helper (patchy-gegl-host) included, is at
https://github.com/SethRobinson/Patchy under the tag of the release.
"@

$outputDir = Split-Path -Parent $Output
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
if (Test-Path -LiteralPath $Output) { Remove-Item -LiteralPath $Output -Force }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($Output, [System.IO.Compression.ZipArchiveMode]::Create)
try {
  $entry = $zip.CreateEntry('README.txt')
  $writer = New-Object System.IO.StreamWriter($entry.Open(), (New-Object System.Text.UTF8Encoding($false)))
  try { $writer.Write(($readme -replace "`r`n", "`n")) } finally { $writer.Dispose() }
  foreach ($item in $entries) {
    # The archives are compressed already.
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $item.Path, "sources/$($item.Name)",
      [System.IO.Compression.CompressionLevel]::NoCompression)
  }
  $scripts = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'scripts\dev\gegl') -File)
  foreach ($file in $scripts) {
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName,
      "build-scripts/scripts/dev/gegl/$($file.Name)", [System.IO.Compression.CompressionLevel]::Optimal)
  }
  [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,
    (Join-Path $repoRoot 'packaging\linux\flatpak\com.rtsoft.patchy.yml'),
    'build-scripts/packaging/linux/flatpak/com.rtsoft.patchy.yml', [System.IO.Compression.CompressionLevel]::Optimal)
} finally {
  $zip.Dispose()
}
$size = (Get-Item -LiteralPath $Output).Length
Write-Host ("Wrote {0} ({1:N0} bytes): {2} source archives, {3} build scripts" -f $Output, $size, $entries.Count, $scripts.Count)
exit 0

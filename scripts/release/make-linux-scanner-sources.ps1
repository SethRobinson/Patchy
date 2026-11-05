<#
Packs the source of the scanner and camera libraries the Flatpak carries (sane-backends and
libgphoto2, docs/import-scanner.md) into build\package\PatchyLinuxScannerSources.zip, which
every release publishes next to its downloads (docs/legal-licensing.md, "Scanner and camera
libraries in the Flatpak").

  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\make-linux-scanner-sources.ps1

The modules, their release tarballs and SHA-256 values come from the Flatpak manifest
(packaging/linux/flatpak/com.rtsoft.patchy.yml), so a version bump there changes the zip and
nothing can go stale. Every archive is checked against the manifest's SHA-256; any file or
patch source a module names is packed too, with the manifest and a README that says which
Patchy release the zip belongs to. Archives come from .deps\linux-scanner-sources (this
checkout's, else the main checkout's) and are downloaded there when missing. Run by
scripts\remote\release-linux.ps1 after the Flatpak bundles are copied back;
publish-github-release.ps1 refuses a release without a current zip. Windows PowerShell 5.1
and pwsh 7 both work. Exit code 0 only when the zip holds every archive the modules name.
#>
param([string]$Output)
$ErrorActionPreference = 'Stop'

function Fail([string]$message) {
  Write-Host "LINUX SCANNER SOURCES FAILED: $message"
  exit 1
}

# The Flatpak modules whose source this zip carries. A module added for the same purpose
# goes here and in publish-github-release.ps1's check, which names the same two.
$moduleNames = @('sane-backends', 'libgphoto2')

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) { Fail "not a Patchy checkout: $repoRoot" }
if (-not $Output) { $Output = Join-Path $repoRoot 'build\package\PatchyLinuxScannerSources.zip' }
$manifestPath = Join-Path $repoRoot 'packaging\linux\flatpak\com.rtsoft.patchy.yml'
$manifestDir = Split-Path -Parent $manifestPath

$cmake = [System.IO.File]::ReadAllText((Join-Path $repoRoot 'CMakeLists.txt'))
$versionMatch = [regex]::Match($cmake, 'project\s*\([\s\S]*?\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})', 'IgnoreCase')
if (-not $versionMatch.Success) { Fail 'could not read the project VERSION from CMakeLists.txt' }
$version = $versionMatch.Groups[1].Value

# The archive cache: this checkout's .deps, else the main checkout's (a worktree has none).
$cache = Join-Path $repoRoot '.deps\linux-scanner-sources'
if (-not (Test-Path -LiteralPath $cache)) {
  $common = (git -C $repoRoot rev-parse --path-format=absolute --git-common-dir 2>$null)
  if ($LASTEXITCODE -eq 0 -and $common) {
    $mainCache = Join-Path (Split-Path -Parent $common.Trim()) '.deps\linux-scanner-sources'
    if (Test-Path -LiteralPath (Split-Path -Parent $mainCache)) { $cache = $mainCache }
  }
}
New-Item -ItemType Directory -Force -Path $cache | Out-Null

function Get-Sha256Hex([string]$path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $stream = [System.IO.File]::OpenRead($path)
  try { $bytes = $sha.ComputeHash($stream) } finally { $stream.Dispose(); $sha.Dispose() }
  return ([System.BitConverter]::ToString($bytes) -replace '-', '').ToLowerInvariant()
}

# Read each module's block from the manifest: its lines (quoted in the README) and its
# sources. The manifest is plain block YAML; a module starts at "  - name: <name>" and runs
# to the next line at that indent or less.
$lines = [System.IO.File]::ReadAllLines($manifestPath)
$modules = [ordered]@{}
$current = $null
foreach ($line in $lines) {
  if ($line -match '^  - name:\s*(\S+)\s*$') {
    $current = if ($moduleNames -contains $Matches[1]) { $Matches[1] } else { $null }
    if ($current) {
      if ($modules.Contains($current)) { Fail "the manifest has two '$current' modules" }
      $modules[$current] = [pscustomobject]@{ Lines = New-Object System.Collections.Generic.List[string]; Sources = New-Object System.Collections.Generic.List[object] }
    }
  } elseif ($current -and $line.Trim() -and $line -notmatch '^    ') {
    $current = $null
  }
  if ($current) { $modules[$current].Lines.Add($line) }
}
foreach ($name in $moduleNames) {
  if (-not $modules.Contains($name)) { Fail "the Flatpak manifest has no '$name' module" }
  $module = $modules[$name]
  $source = $null
  foreach ($line in $module.Lines) {
    if ($line -match '^\s*- type:\s*(\S+)') {
      if ($source) { $module.Sources.Add($source) }
      $source = [pscustomobject]@{ Type = $Matches[1]; Url = ''; Sha = ''; Path = '' }
    } elseif ($source -and $line -match '^\s*url:\s*(\S+)') { $source.Url = $Matches[1] }
    elseif ($source -and $line -match '^\s*sha256:\s*([0-9a-f]{64})') { $source.Sha = $Matches[1] }
    elseif ($source -and $line -match '^\s*path:\s*(\S+)') { $source.Path = $Matches[1] }
    elseif ($source -and $line -match '^\s{4}\S') { $module.Sources.Add($source); $source = $null }
  }
  if ($source) { $module.Sources.Add($source) }
  if (@($module.Sources | Where-Object { $_.Type -eq 'archive' }).Count -eq 0) { Fail "the '$name' module builds from no release archive" }
}

[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$archives = @()
$extraFiles = @()
foreach ($name in $moduleNames) {
  foreach ($source in $modules[$name].Sources) {
    switch ($source.Type) {
      'archive' {
        if (-not $source.Url -or -not $source.Sha) { Fail "an archive of '$name' lacks its url or sha256" }
        $fileName = ($source.Url -split '/')[-1]
        if ($fileName -notmatch '^[A-Za-z0-9._+-]+$') { Fail "unexpected archive name '$fileName' in '$name'" }
        $path = Join-Path $cache $fileName
        if (-not (Test-Path -LiteralPath $path) -or (Get-Sha256Hex $path) -ne $source.Sha) {
          # Three tries: gitlab.com has dropped a first connection now and then.
          for ($attempt = 1; $attempt -le 3; $attempt++) {
            Write-Host "downloading $($source.Url) (try $attempt)"
            try { Invoke-WebRequest -UseBasicParsing -Uri $source.Url -OutFile $path; break }
            catch { if ($attempt -eq 3) { Fail "could not download $($source.Url): $($_.Exception.Message)" } }
          }
        }
        if ((Get-Sha256Hex $path) -ne $source.Sha) { Fail "$fileName does not match its sha256 in the Flatpak manifest" }
        $archives += [pscustomobject]@{ Module = $name; Name = $fileName; Path = $path; Sha = $source.Sha; Url = $source.Url }
      }
      { $_ -in @('file', 'patch') } {
        if (-not $source.Path) { Fail "a $($source.Type) source of '$name' has no path" }
        $full = [System.IO.Path]::GetFullPath((Join-Path $manifestDir $source.Path))
        if (-not $full.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $full -PathType Leaf)) {
          Fail "the $($source.Type) source $($source.Path) of '$name' is not a file in this checkout"
        }
        $relative = $full.Substring($repoRoot.Length).TrimStart('\', '/') -replace '\\', '/'
        $extraFiles += [pscustomobject]@{ Module = $name; Path = $full; Entry = "build-scripts/$relative" }
      }
      default { Fail "the '$name' module has a '$($source.Type)' source this script does not know how to pack" }
    }
  }
}

$archiveList = ($archives | ForEach-Object { "  sources/$($_.Name)`n      sha256 $($_.Sha)`n      from $($_.Url)" }) -join "`n"
$extraList = if ($extraFiles.Count -gt 0) { ($extraFiles | ForEach-Object { "  $($_.Entry) (a $($_.Module) source in the manifest)" }) -join "`n" } else { '  (none)' }
$patchCount = @($moduleNames | ForEach-Object { $modules[$_].Sources } | Where-Object { $_.Type -eq 'patch' }).Count
$patchNote = if ($patchCount -eq 0) { 'Neither library is patched: both are built from the archives above as they are.' } else { "The manifest applies $patchCount patch file(s), listed above." }
$moduleText = ($moduleNames | ForEach-Object { ($modules[$_].Lines | ForEach-Object { $_ }) -join "`n" }) -join "`n"
$readme = @"
Source code of the scanner and camera libraries in Patchy's Flatpak, Patchy $version

Patchy's Flatpak (PatchyLinux.flatpak, PatchyLinuxArm64.flatpak and the Flatpak repository at
rtsoft.com/flatpak) carries two libraries for File > Import > From Scanner or Camera:

  sane-backends  GNU GPL version 2 or later; most backends add SANE's exception that permits
                 linking (see LICENSE in the archive, which also says not every backend does)
  libgphoto2     GNU LGPL version 2.1 or later

This archive is their corresponding source, as those licenses require:

$archiveList

Patches and other files the build uses:
$extraList
$patchNote

How they are built: build-scripts/packaging/linux/flatpak/com.rtsoft.patchy.yml, Patchy's
Flatpak manifest for this release, whose modules are:

$moduleText

The AppImage and other Linux builds of Patchy use the system's copies and carry neither.
Patchy's own source (MIT) is at https://github.com/SethRobinson/Patchy under the tag
v$version.
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
  foreach ($item in $archives) {
    # The archives are compressed already.
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $item.Path, "sources/$($item.Name)",
      [System.IO.Compression.CompressionLevel]::NoCompression)
  }
  foreach ($item in $extraFiles) {
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $item.Path, $item.Entry,
      [System.IO.Compression.CompressionLevel]::Optimal)
  }
  [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $manifestPath,
    'build-scripts/packaging/linux/flatpak/com.rtsoft.patchy.yml', [System.IO.Compression.CompressionLevel]::Optimal)
} finally {
  $zip.Dispose()
}
$size = (Get-Item -LiteralPath $Output).Length
Write-Host ("Wrote {0} ({1:N0} bytes): {2} source archives, {3} other files" -f $Output, $size, $archives.Count, $extraFiles.Count)
exit 0

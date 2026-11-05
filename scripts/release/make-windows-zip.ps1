# Zips a staged Windows package folder for release: the folder itself becomes the zip's
# single top-level folder (build\package\staging\Patchy -> Patchy/...). build-release.bat
# runs it for PatchyWindowsNoInstaller.zip and PatchyWindowsArm64NoInstaller.zip.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\release\make-windows-zip.ps1 -Source <folder> -Output <zip>
#
# Entry names use forward slashes, as the zip format requires. Windows PowerShell 5.1's
# Compress-Archive (Microsoft.PowerShell.Archive 1.0.1.0) writes backslashes, which Windows
# tools accept but libarchive-based Linux tools (Ark, PeaZip) keep as part of each file
# name, unpacking one flat folder of "Patchy\ai\..." files (issue 651). Windows PowerShell
# 5.1 compatible. A file a scanner still holds locked fails the attempt; the whole zip is
# then rebuilt from scratch, up to six times.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Output,
    [int]$Attempts = 6
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$Source = [IO.Path]::GetFullPath($Source).TrimEnd("\")
$Output = [IO.Path]::GetFullPath($Output)
if (-not (Test-Path -LiteralPath $Source -PathType Container)) { Write-Error "Source folder not found: $Source"; exit 1 }
if ($Output.StartsWith($Source + "\", [StringComparison]::OrdinalIgnoreCase)) { Write-Error "Output must not be inside the source folder: $Output"; exit 1 }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$root = Split-Path -Leaf $Source
$parentPrefix = (Split-Path -Parent $Source).TrimEnd("\") + "\"
function Get-EntryName([string]$fullPath) {
    return $fullPath.Substring($parentPrefix.Length).Replace("\", "/")
}

# Ordinal order keeps the entry order the same from run to run.
$directories = @(@($Source) + @(Get-ChildItem -LiteralPath $Source -Recurse -Directory -Force | ForEach-Object { $_.FullName }))
[Array]::Sort($directories, [StringComparer]::Ordinal)
$files = @(Get-ChildItem -LiteralPath $Source -Recurse -File -Force | ForEach-Object { $_.FullName })
[Array]::Sort($files, [StringComparer]::Ordinal)

for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
    try {
        if (Test-Path -LiteralPath $Output) { Remove-Item -LiteralPath $Output -Force }
        $zip = [System.IO.Compression.ZipFile]::Open($Output, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($directory in $directories) {
                [void]$zip.CreateEntry((Get-EntryName $directory) + "/")
            }
            foreach ($file in $files) {
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file, (Get-EntryName $file),
                    [System.IO.Compression.CompressionLevel]::Optimal)
            }
        } finally {
            $zip.Dispose()
        }
        Write-Host ("Wrote {0}: {1} files under {2}/" -f $Output, $files.Count, $root)
        exit 0
    } catch {
        Write-Host ("Zip attempt {0} of {1} failed: {2}" -f $attempt, $Attempts, $_.Exception.Message)
        if ($attempt -lt $Attempts) { Start-Sleep -Seconds 10 }
    }
}
exit 1

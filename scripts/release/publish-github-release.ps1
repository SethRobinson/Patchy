<#
Publishes one Patchy release to GitHub Releases: tag v<version>, the four desktop
artifacts under their stable names, and a SHA256SUMS.txt.

  scripts\release\publish-github-release.bat            (normal release, after release-all.bat)
  scripts\release\publish-github-release.ps1 -Version 0.98 -Target <full sha> -AssetDir <dir>
                                                        (backfill / republish from saved files)

Flow: locate the assets -> write SHA256SUMS.txt -> take the release notes from the
version's "What's New" entry in README.md -> create the release as a DRAFT with the
assets attached -> read the release back and compare every asset's size with the
local file -> only then publish it (which is when GitHub creates the tag) and mark it
Latest. A draft is invisible to visitors, so a failed or partial upload never shows up
as a shipped release. Re-running after a failure reuses the existing draft and
re-uploads with --clobber.

GitHub is the canonical download location; rtsoft.com stays a mirror (upload-to-rtsoft.bat).
The README table and latest_version.json use the /releases/latest/download/<name>
permalinks, which only work because the asset names never change between versions.

Assets, taken from -AssetDir (default build\package):
  PatchyWindowsInstaller.exe, PatchyWindowsNoInstaller.zip           (stable names)
  Patchy-<version>.dmg -> PatchyMacOS.dmg, Patchy-<version>.flatpak -> PatchyLinux.flatpak
  (the mac and Linux builders write versioned files; the stable-name copies that
  upload-mac/linux-to-rtsoft.bat leave in build\package are whatever shipped LAST, so
  they are never used here. When -AssetDir already holds the stable names and no
  versioned file, the stable names are used as-is: that is the backfill case.)

Without -Target the release commit must be HEAD, clean, and already pushed to
origin/main: the tag and latest_version.json both have to point at the commit that
produced the build. With -Target (backfill), the commit only has to be on origin/main.

Requires gh (authenticated: gh auth status) and git on PATH. Windows PowerShell 5.1 and
pwsh 7 both work. Exit code 0 only after the release is public and verified.
#>
param(
  [string]$Version,
  [string]$Target,
  [string]$AssetDir,
  [string]$Repo = 'SethRobinson/Patchy'
)
# 'Continue', not 'Stop': native stderr chatter (git, gh) would otherwise become a
# terminating NativeCommandError under Windows PowerShell 5.1. Every native call
# below checks $LASTEXITCODE explicitly.
$ErrorActionPreference = 'Continue'

function Fail([string]$message) {
  Write-Host ''
  Write-Host "GITHUB RELEASE FAILED: $message"
  Write-Host 'Nothing public changed unless a line above says the release was published.'
  exit 1
}

$repoRoot = (git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $repoRoot) { Fail 'not inside the Patchy git checkout' }
$repoRoot = $repoRoot.Trim()
Set-Location $repoRoot

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Fail 'gh (GitHub CLI) is not on PATH' }
gh auth status 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Fail 'gh is not logged in (run: gh auth login)' }

# Version: same extraction as build-release.bat, from CMakeLists.txt.
if (-not $Version) {
  $cmake = Get-Content -Raw -LiteralPath 'CMakeLists.txt'
  $match = [regex]::Match($cmake, 'project\s*\([\s\S]*?\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})', 'IgnoreCase')
  if (-not $match.Success) { Fail 'could not read the project VERSION from CMakeLists.txt' }
  $Version = $match.Groups[1].Value
}
if ($Version -notmatch '^[0-9]+(\.[0-9]+){1,3}$') { Fail "'$Version' is not a dotted version" }
$tag = "v$Version"
$title = "Patchy $Version"

# Commit the tag will point at.
git fetch origin --quiet
if ($LASTEXITCODE -ne 0) { Fail 'git fetch origin failed' }
if ($Target) {
  $sha = (git rev-parse --verify "$Target^{commit}" 2>$null)
  if ($LASTEXITCODE -ne 0 -or -not $sha) { Fail "-Target '$Target' is not a commit in this checkout" }
  $sha = $sha.Trim()
  git merge-base --is-ancestor $sha origin/main
  if ($LASTEXITCODE -ne 0) { Fail "target commit $sha is not on origin/main (push it first)" }
} else {
  $dirty = (git status --porcelain --untracked-files=no)
  if ($dirty) { Fail 'the working tree has uncommitted changes; commit the release first' }
  $sha = (git rev-parse HEAD).Trim()
  $remote = (git rev-parse origin/main).Trim()
  if ($sha -ne $remote) { Fail "HEAD ($sha) is not origin/main ($remote); push the release commit first" }
}
Write-Host "Release $tag from commit $sha"

# Assets.
if (-not $AssetDir) { $AssetDir = Join-Path $repoRoot 'build\package' }
if (-not (Test-Path -LiteralPath $AssetDir -PathType Container)) { Fail "asset directory not found: $AssetDir" }
$AssetDir = (Resolve-Path -LiteralPath $AssetDir).Path

function Resolve-Asset([string]$stableName, [string]$versionedName) {
  if ($versionedName) {
    $versioned = Join-Path $AssetDir $versionedName
    if (Test-Path -LiteralPath $versioned -PathType Leaf) { return $versioned }
    # Any other versioned file in the folder means the build for this version failed
    # or was skipped; the stable name would then be last release's staging copy.
    $stale = @(Get-ChildItem -LiteralPath $AssetDir -Filter ($versionedName -replace [regex]::Escape($Version), '*') -File)
    if ($stale.Count -gt 0) {
      Fail "$versionedName is missing but $($stale[0].Name) is present: rebuild for $Version"
    }
  }
  $stable = Join-Path $AssetDir $stableName
  if (Test-Path -LiteralPath $stable -PathType Leaf) { return $stable }
  Fail "missing release asset: $stableName in $AssetDir"
}

$assets = [ordered]@{
  'PatchyWindowsInstaller.exe'   = Resolve-Asset 'PatchyWindowsInstaller.exe' $null
  'PatchyWindowsNoInstaller.zip' = Resolve-Asset 'PatchyWindowsNoInstaller.zip' $null
  'PatchyMacOS.dmg'              = Resolve-Asset 'PatchyMacOS.dmg' "Patchy-$Version.dmg"
  'PatchyLinux.flatpak'          = Resolve-Asset 'PatchyLinux.flatpak' "Patchy-$Version.flatpak"
}

# Stage under the published names in a scratch folder (gh names an asset after its
# file), never inside build\package: the stable names there belong to the rtsoft
# upload scripts.
$stage = Join-Path $repoRoot "build\github-release\$tag"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
$sums = New-Object System.Collections.Generic.List[string]
foreach ($name in $assets.Keys) {
  $source = $assets[$name]
  $dest = Join-Path $stage $name
  Copy-Item -LiteralPath $source -Destination $dest
  $hash = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
  $sums.Add("$hash  $name")
  Write-Host ("  {0,-30} {1,12:N0} bytes  sha256 {2}  <- {3}" -f $name, (Get-Item -LiteralPath $dest).Length, $hash, $source)
}
$sumsPath = Join-Path $stage 'SHA256SUMS.txt'
[IO.File]::WriteAllText($sumsPath, (($sums -join "`n") + "`n"))
$uploads = @($assets.Keys | ForEach-Object { Join-Path $stage $_ }) + @($sumsPath)

# Release notes: this version's "What's New" entry (README.md, or RELEASE-HISTORY.md
# once it has moved there) plus the download table.
function Get-WhatsNew([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return $null }
  $lines = Get-Content -LiteralPath $path
  $start = -1
  for ($i = 0; $i -lt $lines.Count; $i++) {
    # README uses "### <version> - <date>", RELEASE-HISTORY.md "## <version> - <date>".
    if ($lines[$i] -match ('^#{2,3} ' + [regex]::Escape($Version) + '(\s|$)')) { $start = $i; break }
  }
  if ($start -lt 0) { return $null }
  $body = New-Object System.Collections.Generic.List[string]
  for ($i = $start + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^#{1,3} ' -or $lines[$i] -match '^\[Older releases\]') { break }
    $body.Add($lines[$i])
  }
  $heading = $lines[$start] -replace '^#{2,3} ', ''
  return @{ heading = $heading; body = (($body -join "`n").Trim()) }
}
$notes = Get-WhatsNew 'README.md'
if (-not $notes) { $notes = Get-WhatsNew 'RELEASE-HISTORY.md' }
$notesText = ''
if ($notes -and $notes.body) {
  $notesText = "## What's new in " + $notes.heading + "`n`n" + $notes.body + "`n`n"
} else {
  Write-Host "WARNING: no '### $Version' entry found in README.md or RELEASE-HISTORY.md; notes carry only the download table"
}
$base = "https://github.com/$Repo/releases/download/$tag"
$notesText += @"
## Downloads

| Platform | File |
| --- | --- |
| Windows 10/11 (64-bit) installer | [PatchyWindowsInstaller.exe]($base/PatchyWindowsInstaller.exe) |
| Windows 10/11 (64-bit) portable zip | [PatchyWindowsNoInstaller.zip]($base/PatchyWindowsNoInstaller.zip) |
| macOS 12+ (Apple Silicon) | [PatchyMacOS.dmg]($base/PatchyMacOS.dmg) |
| Linux Flatpak bundle | [PatchyLinux.flatpak]($base/PatchyLinux.flatpak) |

Windows builds are code signed; the macOS app is signed and notarized. Checksums are in [SHA256SUMS.txt]($base/SHA256SUMS.txt). The same files are mirrored at rtsoft.com, and the browser version runs at [patchyimageeditor.com](https://www.patchyimageeditor.com).
"@
$notesPath = Join-Path $stage 'release-notes.md'
[IO.File]::WriteAllText($notesPath, $notesText)

# Create the draft, or reuse one left by a failed run. A public release with this tag
# is never touched: bump the version instead.
$existing = gh release view $tag --repo $Repo --json isDraft,url 2>$null | ConvertFrom-Json
if ($LASTEXITCODE -eq 0 -and $existing) {
  if (-not $existing.isDraft) { Fail "release $tag already exists and is public: $($existing.url)" }
  Write-Host "Reusing draft release $tag; re-uploading assets"
  gh release upload $tag --repo $Repo --clobber @uploads
  if ($LASTEXITCODE -ne 0) { Fail 'asset upload failed' }
  gh release edit $tag --repo $Repo --target $sha --title $title --notes-file $notesPath | Out-Null
  if ($LASTEXITCODE -ne 0) { Fail 'updating the draft failed' }
} else {
  Write-Host "Creating draft release $tag ($($uploads.Count) assets)"
  gh release create $tag --repo $Repo --draft --target $sha --title $title --notes-file $notesPath @uploads
  if ($LASTEXITCODE -ne 0) { Fail 'gh release create failed (the draft, if any, can be reused by re-running)' }
}

# Verify what landed before anyone can see it.
$view = gh release view $tag --repo $Repo --json assets,isDraft,url 2>$null | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or -not $view) { Fail 'could not read the draft back' }
$bad = @()
foreach ($path in $uploads) {
  $name = [IO.Path]::GetFileName($path)
  $local = Get-Item -LiteralPath $path
  $remote = @($view.assets | Where-Object { $_.name -eq $name })
  if ($remote.Count -ne 1) { $bad += "$name missing on GitHub"; continue }
  if ([int64]$remote[0].size -ne $local.Length) { $bad += "$name size $($remote[0].size) on GitHub, $($local.Length) locally" }
}
$expected = $uploads.Count
if (@($view.assets).Count -ne $expected) { $bad += "expected $expected assets, GitHub has $(@($view.assets).Count)" }
if ($bad.Count -gt 0) { Fail ("draft verification failed: " + ($bad -join '; ') + ". Re-run to re-upload.") }
Write-Host "Verified $expected assets on the draft"

gh release edit $tag --repo $Repo --draft=false --latest | Out-Null
if ($LASTEXITCODE -ne 0) { Fail 'publishing the draft failed; it is still a draft on GitHub' }
$final = gh release view $tag --repo $Repo --json isDraft,url,tagName 2>$null | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or -not $final -or $final.isDraft) { Fail 'release did not become public' }
git fetch origin --tags --quiet
Write-Host ''
Write-Host "Published $($final.tagName): $($final.url)"
Write-Host "Latest permalinks: https://github.com/$Repo/releases/latest/download/<asset name>"
exit 0

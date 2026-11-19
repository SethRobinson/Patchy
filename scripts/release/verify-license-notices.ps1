# Compare the package's license files with this checkout and the target Qt kit.
param(
    [Parameter(Mandatory = $true)][string]$Directory,
    [switch]$Web,
    [string]$QtDeclarativeNotice
)
$ErrorActionPreference = 'Stop'

# Get-FileHash can be unavailable in Windows PowerShell launched from pwsh 7.
function Get-NoticeHash([string]$Path) {
    $stream = [IO.File]::OpenRead((Get-Item -LiteralPath $Path).FullName)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream))
    } finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sources = [ordered]@{
    'LICENSE' = Join-Path $repoRoot 'LICENSE'
    'NOTICE-THIRD-PARTY.md' = Join-Path $repoRoot 'NOTICE-THIRD-PARTY.md'
}
if ($Web) {
    $sources['libheif-COPYING.txt'] = Join-Path $repoRoot 'src/formats/libheif/COPYING'
}
if ($QtDeclarativeNotice) {
    $sources["licenses/qt/$(Split-Path -Leaf $QtDeclarativeNotice)"] = $QtDeclarativeNotice
}
foreach ($name in $sources.Keys) {
    $staged = Join-Path $Directory $name
    if (-not (Test-Path -LiteralPath $staged -PathType Leaf)) {
        throw "Missing license file: $staged"
    }
    $source = $sources[$name]
    if ((Get-NoticeHash $staged) -ne (Get-NoticeHash $source)) {
        throw "License file $staged differs from its source $source"
    }
}

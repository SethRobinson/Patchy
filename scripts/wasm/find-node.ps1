# Resolves the emsdk-bundled node.exe that runs Patchy's WebAssembly tooling: the
# wasm-core test suite, the local site servers and the site precompressor. This is
# the one place the rule lives (docs/wasm.md "Node resolution"); every script that
# needs node calls it instead of globbing on its own.
#
# From PowerShell:
#   $NodeExe = & scripts\wasm\find-node.ps1
# From a batch file (stdout is exactly one line, the full path; nothing on failure):
#   for /f "usebackq delims=" %%N in (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\wasm\find-node.ps1`) do set "NODE_EXE=%%N"
#
# Rule:
# 1. The NODE_JS line of .deps\emsdk\.emscripten, which `emsdk activate` writes for
#    the node package the activated release depends on. emsdk 4.0.7 installs node
#    24.19.0, laid out as node\24.19.0_64bit\node.exe; the 22.x and older packages
#    used node\<version>_64bit\bin\node.exe.
# 2. Without a usable activation, the newest node\<version>_64bit\ directory that
#    holds node.exe at its root or under bin\.
# The wasm-core preset cannot call a script, so CMakePresets.json pins the same
# file as a literal CMAKE_CROSSCOMPILING_EMULATOR path; setup-emsdk.ps1 fails when
# the two disagree.
param(
  [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)
$ErrorActionPreference = 'Stop'

$EmsdkDir = Join-Path $RepoRoot '.deps\emsdk'
$EmscriptenConfig = Join-Path $EmsdkDir '.emscripten'

if (Test-Path $EmscriptenConfig) {
  $NodeLine = Get-Content $EmscriptenConfig | Where-Object { $_ -match '^\s*NODE_JS\s*=' } | Select-Object -First 1
  if ($NodeLine) {
    # Either `emsdk_path + '/node/<ver>/node.exe'` or a quoted absolute path.
    $Quoted = @([regex]::Matches($NodeLine, "'([^']*)'") | ForEach-Object { $_.Groups[1].Value })
    if ($Quoted.Count -ge 1) {
      $Candidate = if ($NodeLine -match 'emsdk_path') { Join-Path $EmsdkDir ($Quoted[-1].TrimStart('/')) } else { $Quoted[-1] }
      if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
        Write-Output (Resolve-Path -LiteralPath $Candidate).Path
        exit 0
      }
    }
  }
}

$NodeRoot = Join-Path $EmsdkDir 'node'
if (Test-Path $NodeRoot) {
  $Dirs = Get-ChildItem -LiteralPath $NodeRoot -Directory | Sort-Object -Descending {
    $v = $null
    if ($_.Name -match '^(\d+(\.\d+){0,3})') { $v = [version]$Matches[1] }
    if ($null -eq $v) { [version]'0.0' } else { $v }
  }
  foreach ($Dir in $Dirs) {
    foreach ($Leaf in @('node.exe', 'bin\node.exe')) {
      $Candidate = Join-Path $Dir.FullName $Leaf
      if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
        Write-Output $Candidate
        exit 0
      }
    }
  }
}

throw "The emsdk-bundled node was not found under $NodeRoot; run scripts\wasm\setup-emsdk.ps1 first"

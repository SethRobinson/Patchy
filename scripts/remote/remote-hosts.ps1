<#
Resolves the ssh target of a remote build machine. Dot-source it, then:

  . "$PSScriptRoot\remote-hosts.ps1"
  $remote = Get-PatchyRemoteHost mac        # .ssh, plus .workTree for windows

Machine names, users, and paths are developer-specific, so they live in
scripts\remote\hosts.local.json (gitignored; copy hosts.example.json) and never in the
public repository. A git worktree has no untracked files, so when the checkout running
the script has no hosts.local.json, the main checkout's copy is used.
#>
function Get-PatchyRemoteHost {
  param([Parameter(Mandatory = $true)][ValidateSet('mac', 'linux', 'windows')][string]$Target)

  $candidates = @(Join-Path $PSScriptRoot 'hosts.local.json')
  $commonDir = git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir 2>$null
  if ($commonDir) {
    $candidates += Join-Path (Split-Path $commonDir.Trim()) 'scripts\remote\hosts.local.json'
  }
  foreach ($path in $candidates) {
    if (-not (Test-Path $path)) { continue }
    $entry = (Get-Content $path -Raw | ConvertFrom-Json).$Target
    if (-not $entry -or -not $entry.ssh) {
      throw "$path has no '$Target' entry with an 'ssh' value (see scripts\remote\hosts.example.json)"
    }
    if ($Target -eq 'windows' -and -not $entry.workTree) {
      throw "$path 'windows' entry needs a 'workTree' (see scripts\remote\hosts.example.json)"
    }
    return $entry
  }
  throw ("No hosts.local.json (looked in $($candidates -join ', ')). Copy " +
    'scripts\remote\hosts.example.json to hosts.local.json and fill in the machines this checkout can ssh into.')
}

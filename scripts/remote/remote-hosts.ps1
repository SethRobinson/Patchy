<#
Resolves the ssh target of a remote build machine. Dot-source it, then:

  . "$PSScriptRoot\remote-hosts.ps1"
  $remote = Get-PatchyRemoteHost mac        # .ssh, plus .workTree for windows
  $hosts = Get-PatchyWindowsHosts           # every windows entry, in preference order
  $arm = Get-PatchyWindowsHosts windows-arm64   # the Windows ARM64 hosts, same shape
  $web = Get-PatchyWindowsHosts wasm        # the Windows hosts that build the web site

Machine names, users, and paths are developer-specific, so they live in
scripts\remote\hosts.local.json (gitignored; copy hosts.example.json) and never in the
public repository. A git worktree has no untracked files, so when the checkout running
the script has no hosts.local.json, the main checkout's copy is used.

The windows entry may be one object or an ordered list of them (first choice first);
remote-build.ps1 -Target windows uses the first one that is reachable and idle, and
Get-PatchyRemoteHost windows returns the first. windows-arm64 lists the Windows ARM64 hosts
the same way; it is a separate entry so an x64 build never lands on one. wasm lists the
Windows hosts that build the web release (remote-build.ps1 -Target wasm, release-wasm.ps1),
each also with a 'share': the UNC path of its work tree, which the site is copied back from.
#>
function Get-PatchyHostsEntry {
  param([Parameter(Mandatory = $true)][ValidateSet('mac', 'linux', 'windows', 'windows-arm64', 'wasm')][string]$Target)

  $candidates = @(Join-Path $PSScriptRoot 'hosts.local.json')
  $commonDir = git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir 2>$null
  if ($commonDir) {
    $candidates += Join-Path (Split-Path $commonDir.Trim()) 'scripts\remote\hosts.local.json'
  }
  foreach ($path in $candidates) {
    if (-not (Test-Path $path)) { continue }
    # @() keeps a single object and a list alike; ConvertFrom-Json unrolls one-element arrays.
    $entries = @((Get-Content $path -Raw | ConvertFrom-Json).$Target)
    if ($entries.Count -eq 0 -or -not $entries[0]) {
      throw "$path has no '$Target' entry with an 'ssh' value (see scripts\remote\hosts.example.json)"
    }
    foreach ($entry in $entries) {
      if (-not $entry.ssh) {
        throw "$path has a '$Target' entry without an 'ssh' value (see scripts\remote\hosts.example.json)"
      }
      if (($Target -like 'windows*' -or $Target -eq 'wasm') -and -not $entry.workTree) {
        throw "$path '$Target' entry '$($entry.ssh)' needs a 'workTree' (see scripts\remote\hosts.example.json)"
      }
      if ($Target -eq 'wasm' -and -not $entry.share) {
        throw "$path 'wasm' entry '$($entry.ssh)' needs a 'share' (see scripts\remote\hosts.example.json)"
      }
    }
    return , $entries
  }
  throw ("No hosts.local.json (looked in $($candidates -join ', ')). Copy " +
    'scripts\remote\hosts.example.json to hosts.local.json and fill in the machines this checkout can ssh into.')
}

function Get-PatchyRemoteHost {
  param([Parameter(Mandatory = $true)][ValidateSet('mac', 'linux', 'windows', 'windows-arm64', 'wasm')][string]$Target)
  return (Get-PatchyHostsEntry $Target)[0]
}

function Get-PatchyWindowsHosts {
  param([ValidateSet('windows', 'windows-arm64', 'wasm')][string]$Target = 'windows')
  return , (Get-PatchyHostsEntry $Target)
}

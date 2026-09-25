<#
Snapshots the current working tree (uncommitted changes included) as a throwaway commit
and force-pushes it to a ref on a remote machine's bare repo (~/patchy.git or the Windows
equivalent). Dot-source it from a script that has already Push-Location'd to the repo root:

  . "$PSScriptRoot\remote-snapshot.ps1"
  $sha = Push-PatchySnapshot -RemoteHost 'user@host' [-Ref 'refs/snapshots/dev']

No commit or branch is created in this repository and the real index is untouched: a
temporary index (PID-suffixed, so concurrent pushes to different hosts do not clobber each
other) takes `git add -A`, write-tree and commit-tree produce the snapshot, and the temporary
index is removed again. The remote side checks the ref out detached, so unchanged files keep
their mtimes and the remote Ninja build stays incremental. Throws on any git failure.
Shared by remote-build.ps1 (mac/linux/windows test builds) and build-wasm-st-remote.ps1.
#>
function Push-PatchySnapshot {
  param(
    [Parameter(Mandatory = $true)][string]$RemoteHost,
    [string]$Ref = 'refs/snapshots/dev'
  )
  $gitDir = (git rev-parse --absolute-git-dir).Trim()
  $tmpIndex = Join-Path $gitDir "patchy-remote-$PID.index"
  $env:GIT_INDEX_FILE = $tmpIndex
  try {
    git read-tree HEAD
    if ($LASTEXITCODE -ne 0) { throw 'git read-tree failed' }
    # Filter the per-file autocrlf normalization notices (pre-existing working-copy line
    # endings vs .gitattributes); anything else on stderr still shows.
    git add -A 2>&1 | Where-Object { $_ -notmatch 'will be replaced by (LF|CRLF)' } | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw 'git add failed' }
    $tree = (git write-tree).Trim()
    $snap = (git commit-tree $tree -p HEAD -m 'patchy remote build snapshot').Trim()
    if (-not $snap) { throw 'git commit-tree failed' }
  }
  finally {
    Remove-Item Env:GIT_INDEX_FILE -ErrorAction SilentlyContinue
    if (Test-Path $tmpIndex) { Remove-Item $tmpIndex -Force -ErrorAction SilentlyContinue }
  }

  Write-Host "== pushing snapshot $($snap.Substring(0, 12)) to $RemoteHost ($Ref) =="
  git push --force --quiet "${RemoteHost}:patchy.git" "${snap}:${Ref}"
  if ($LASTEXITCODE -ne 0) { throw 'git push to the remote bare repo failed (run the setup script first?)' }
  return $snap
}

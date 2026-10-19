# Per-host build lock for remote-build.ps1 (dot-source it). Each remote host has ONE work tree
# and one refs/snapshots/dev ref, so two runs on the same host overwrite each other's snapshot
# or build directory. A run takes the lock BEFORE it pushes its snapshot and holds it until its
# artifacts are fetched; a second run on that host refuses (exit 75) instead of starting.
#
# The lock is a directory beside the remote work tree (`<parent of workTree>/remote-build.lock`
# on Windows, `~/patchy/remote-build.lock` on mac/linux): creating a directory is atomic, so two
# runs can never both take it. owner.txt inside names the holder. A lock left behind by a run
# from THIS machine whose process has exited is stale and is taken over; any other leftover lock
# needs -BreakLock after checking that its owner is really gone.

function Get-PatchyLockOwner([string]$RepoRoot) {
  $tree = ($RepoRoot -replace '[^\w:/\\.\-]', '_')
  "machine=$env:COMPUTERNAME pid=$PID tree=$tree since=$(Get-Date -Format s)"
}

function Get-PatchyLockPath([bool]$Windows, $Remote) {
  if ($Windows) {
    $wt = $Remote.workTree.Replace('\', '/').TrimEnd('/')
    return ($wt.Substring(0, $wt.LastIndexOf('/')) + '/remote-build.lock')
  }
  '~/patchy/remote-build.lock'
}

function Invoke-PatchyLockCommand([string]$RemoteHost, [bool]$Windows, [string]$Action, [string]$LockPath, [string]$Owner) {
  # Single quotes only inside the remote command: local PowerShell mangles embedded double quotes
  # in native-command arguments (see remote-build.ps1). The owner string carries no quotes.
  if ($Windows) {
    $cmd = switch ($Action) {
      'acquire' { "try { New-Item -ItemType Directory -Path '$LockPath' -ErrorAction Stop | Out-Null; Set-Content -LiteralPath '$LockPath/owner.txt' -Value '$Owner'; 'patchy-lock-acquired' } catch { 'patchy-lock-held'; Get-Content -LiteralPath '$LockPath/owner.txt' -ErrorAction SilentlyContinue }" }
      'release' { "if ((Get-Content -LiteralPath '$LockPath/owner.txt' -ErrorAction SilentlyContinue) -eq '$Owner') { Remove-Item -LiteralPath '$LockPath' -Recurse -Force; 'patchy-lock-released' } else { 'patchy-lock-not-ours' }" }
      'break'   { "Remove-Item -LiteralPath '$LockPath' -Recurse -Force -ErrorAction SilentlyContinue; 'patchy-lock-broken'" }
    }
  }
  else {
    $cmd = switch ($Action) {
      'acquire' { "if mkdir $LockPath 2>/dev/null; then printf '%s\n' '$Owner' > $LockPath/owner.txt; echo patchy-lock-acquired; else echo patchy-lock-held; cat $LockPath/owner.txt 2>/dev/null; fi" }
      'release' { "if grep -qxF '$Owner' $LockPath/owner.txt 2>/dev/null; then rm -rf ~/patchy/remote-build.lock; echo patchy-lock-released; else echo patchy-lock-not-ours; fi" }
      'break'   { "rm -rf ~/patchy/remote-build.lock; echo patchy-lock-broken" }
    }
  }
  @(ssh -o ConnectTimeout=10 -o BatchMode=yes $RemoteHost $cmd 2>$null)
}

# Returns $null when the lock is ours, otherwise a one-line reason the host is taken.
function Enter-PatchyRemoteLock([string]$RemoteHost, [bool]$Windows, [string]$LockPath, [string]$Owner, [switch]$BreakLock) {
  if ($BreakLock) { Invoke-PatchyLockCommand $RemoteHost $Windows 'break' $LockPath $Owner | Out-Null }
  for ($attempt = 0; $attempt -lt 2; $attempt++) {
    $out = Invoke-PatchyLockCommand $RemoteHost $Windows 'acquire' $LockPath $Owner
    if ($out -contains 'patchy-lock-acquired') { return $null }
    if ($out -notcontains 'patchy-lock-held') { return "could not reach $RemoteHost to take its build lock" }
    $holder = (@($out | Where-Object { $_ -and $_ -ne 'patchy-lock-held' }) -join ' ').Trim()
    # Stale: taken by a run on this machine whose process is gone (killed, or the window closed).
    if ($attempt -eq 0 -and $holder -match "^machine=$([regex]::Escape($env:COMPUTERNAME)) pid=(\d+) " -and
        -not (Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue)) {
      Write-Host "== $RemoteHost build lock is stale ($holder); taking it over =="
      Invoke-PatchyLockCommand $RemoteHost $Windows 'break' $LockPath $Owner | Out-Null
      continue
    }
    if (-not $holder) { $holder = 'another run that is just starting' }
    return "$RemoteHost is locked by $holder"
  }
  "$RemoteHost build lock could not be taken"
}

function Exit-PatchyRemoteLock([string]$RemoteHost, [bool]$Windows, [string]$LockPath, [string]$Owner) {
  $out = Invoke-PatchyLockCommand $RemoteHost $Windows 'release' $LockPath $Owner
  if ($out -notcontains 'patchy-lock-released') { Write-Host "== warning: $RemoteHost build lock was not released ($($out -join ' ')) ==" }
}

# Per-host build lock and busy probe for remote-build.ps1 (dot-source it). Each remote host has
# ONE work tree and one refs/snapshots/dev ref, so two runs on the same host overwrite each
# other's snapshot or build directory. A run takes the lock BEFORE it pushes its snapshot and
# holds it until its artifacts are fetched; a second run on that host refuses (exit 75) instead
# of starting.
#
# The lock is a directory beside the remote work tree (`<parent of workTree>/remote-build.lock`
# on Windows, `~/patchy/remote-build.lock` on mac/linux): creating a directory is atomic, so two
# runs can never both take it. owner.txt inside names the holder.
#
# A leftover lock is taken over automatically only when BOTH halves of its run are gone: the
# holder is a run from THIS machine whose PowerShell process has exited (or whose PID now
# belongs to a process started after the lock was taken), AND the host shows no build or test
# processes (Get-PatchyRemoteBuildState). The second check matters because a remote build can
# outlive its ssh client: killing the local run (or closing its window) does not stop the
# remote cl/link/ninja or test suite, and taking the lock over then would start a second build
# beside it. Any other leftover lock needs -BreakLock after checking that its owner is really
# gone.

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

# Probes one remote host for build and test processes. Returns 'idle', 'busy: <process names>',
# or 'unreachable'. The marker line, not the exit code, says whether the probe ran: on Windows
# Get-Process with absent names fails, and pgrep exits 1 when nothing matches.
#
# Windows: the remote shell is Windows PowerShell 5.1 (setup-windows-ssh.ps1 sets it). Single
# quotes only: no local expansion, and local PowerShell mangles embedded double quotes.
# mac/linux: pgrep matches the process NAME (never -f, which would match this probe's own shell
# command line). Linux truncates names to 15 characters, hence the patchy_* prefixes. Processes
# inside the Flatpak SDK sandbox are visible to the host's pgrep.
function Get-PatchyRemoteBuildState([string]$RemoteHost, [bool]$Windows) {
  if ($Windows) {
    $probe = 'Get-Process cl,link,ninja,cmake,patchy_core_tests,patchy_ui_visual_tests -ErrorAction SilentlyContinue | ForEach-Object { $_.Name } | Sort-Object -Unique; ''patchy-probe-ok'''
  }
  else {
    $probe = "pgrep -l '^(ninja|cmake|cc1plus|cc1|clang|clang\+\+|ld|ld64|ld\.lld|ld\.gold|collect2|lto1|patchy_core_tes.*|patchy_ui_visua.*|patchy_perf_tes.*|patchy_mac_a11y.*)`$' | awk '{print `$2}' | sort -u; echo patchy-probe-ok"
  }
  $out = @(ssh -o ConnectTimeout=8 -o BatchMode=yes $RemoteHost $probe 2>$null)
  if ($out -notcontains 'patchy-probe-ok') { return 'unreachable' }
  $busy = @($out | Where-Object { $_ -and $_ -ne 'patchy-probe-ok' })
  if ($busy.Count -gt 0) { return "busy: $($busy -join ', ')" }
  return 'idle'
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

# True when the lock holder names a run on this machine whose process is gone. A PID that is
# alive again but belongs to a process started after the lock was taken is a reused PID: the
# holder is gone too.
function Test-PatchyLockHolderGone([string]$Holder) {
  if ($Holder -notmatch "^machine=$([regex]::Escape($env:COMPUTERNAME)) pid=(\d+) ") { return $false }
  $proc = Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue
  if (-not $proc) { return $true }
  if ($Holder -match ' since=(\S+)$') {
    $since = [datetime]::MinValue
    if ([datetime]::TryParse($Matches[1], [ref]$since) -and $proc.StartTime -gt $since.AddSeconds(1)) { return $true }
  }
  $false
}

# Returns $null when the lock is ours, otherwise a one-line reason the host is taken.
function Enter-PatchyRemoteLock([string]$RemoteHost, [bool]$Windows, [string]$LockPath, [string]$Owner, [switch]$BreakLock) {
  if ($BreakLock) { Invoke-PatchyLockCommand $RemoteHost $Windows 'break' $LockPath $Owner | Out-Null }
  for ($attempt = 0; $attempt -lt 2; $attempt++) {
    $out = Invoke-PatchyLockCommand $RemoteHost $Windows 'acquire' $LockPath $Owner
    if ($out -contains 'patchy-lock-acquired') { return $null }
    if ($out -notcontains 'patchy-lock-held') { return "could not reach $RemoteHost to take its build lock" }
    $holder = (@($out | Where-Object { $_ -and $_ -ne 'patchy-lock-held' }) -join ' ').Trim()
    # Stale: taken by a run on this machine whose process is gone (killed, or the window closed),
    # and nothing of that run is still building or testing on the host.
    if ($attempt -eq 0 -and (Test-PatchyLockHolderGone $holder)) {
      $state = Get-PatchyRemoteBuildState $RemoteHost $Windows
      if ($state -ne 'idle') {
        return "$RemoteHost is locked by $holder; that run's local process is gone, but the host is $state (its remote build may still be running)"
      }
      Write-Host "== $RemoteHost build lock is stale ($holder) and the host is idle; taking it over =="
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

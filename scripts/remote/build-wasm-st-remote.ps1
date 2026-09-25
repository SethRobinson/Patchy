<#
Builds the wasm-release-st preset (the single-threaded Qt for WebAssembly app served to
Safari) on the Windows offload host and copies the result into this checkout's
build\wasm-release-st, so scripts\release\build-wasm.bat can build the multithreaded
variant locally at the same time instead of one after the other.

  scripts\remote\build-wasm-st-remote.ps1 [-Marker <file>] [-Jobs 8]

Mechanism: the working tree (uncommitted changes included) is snapshotted and pushed as
refs/snapshots/wasm-st to the offload host's bare repo (remote-snapshot.ps1), the host's
work tree checks it out detached (its wasm-release-st build directory stays warm, so a
small change rebuilds in minutes), scripts\remote\build-wasm-st.ps1 configures and builds
there, and scp brings patchy.js, patchy.wasm, patchy.data and qtloader.js back. The ssh
target and work tree come from hosts.local.json's "windows" entry (remote-hosts.ps1); the
same work tree serves remote-build.ps1 -Target windows, so do not run both at once.

-Marker names a file that receives exit=<code> when this script ends, whatever happened,
so a batch file that started it in the background (build-wasm.bat) can wait for it and
judge it. The exit code is the same value.
#>
param(
  [string]$Marker = '',
  [int]$Jobs = 8
)

$ErrorActionPreference = 'Continue'
$code = 1
$started = Get-Date
try {
  . "$PSScriptRoot\remote-hosts.ps1"
  . "$PSScriptRoot\remote-snapshot.ps1"
  $remote = Get-PatchyRemoteHost windows
  $remoteHost = $remote.ssh
  $wt = $remote.workTree.Replace('\', '/')

  $repoRoot = (git rev-parse --show-toplevel 2>$null)
  if (-not $repoRoot) { throw 'build-wasm-st-remote.ps1 must run inside the Patchy repository' }
  Push-Location $repoRoot.Trim()
  try {
    $snap = Push-PatchySnapshot -RemoteHost $remoteHost -Ref 'refs/snapshots/wasm-st'
    Write-Host "== building wasm-release-st snapshot $($snap.Substring(0, 12)) on $remoteHost =="
    # A Windows OpenSSH shell is Windows PowerShell 5.1: no &&, every step checked explicitly.
    $remoteCmd = "git -C $wt fetch -q origin +refs/snapshots/wasm-st; " + 'if ($LASTEXITCODE) { exit $LASTEXITCODE }; ' +
      "git -C $wt checkout -q -f --detach FETCH_HEAD; " + 'if ($LASTEXITCODE) { exit $LASTEXITCODE }; ' +
      "powershell -NoProfile -ExecutionPolicy Bypass -File $wt/scripts/remote/build-wasm-st.ps1 -Jobs $Jobs; " + 'exit $LASTEXITCODE'
    ssh $remoteHost $remoteCmd
    if ($LASTEXITCODE -ne 0) { throw "the remote wasm-release-st build failed ($LASTEXITCODE)" }

    $dest = Join-Path (Get-Location) 'build\wasm-release-st'
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($name in @('patchy.js', 'patchy.wasm', 'patchy.data', 'qtloader.js')) {
      $target = Join-Path $dest $name
      if (Test-Path $target) { Remove-Item $target -Force }
      Write-Host "== fetching $name =="
      scp -q "${remoteHost}:$wt/build/wasm-release-st/$name" $target
      if ($LASTEXITCODE -ne 0 -or -not (Test-Path $target) -or (Get-Item $target).Length -eq 0) {
        throw "could not copy $name back from $remoteHost"
      }
    }
    Get-ChildItem $dest | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize | Out-String | Write-Host
    $code = 0
    Write-Host ("== wasm-release-st ready in build\wasm-release-st after {0:n0} s ==" -f ((Get-Date) - $started).TotalSeconds)
  }
  finally {
    Pop-Location
  }
}
catch {
  Write-Host "build-wasm-st-remote.ps1: $($_.Exception.Message)"
  if ($code -eq 0) { $code = 1 }
}
finally {
  if ($Marker) {
    Set-Content -LiteralPath $Marker -Value "exit=$code" -Encoding ASCII
  }
}
exit $code

<#
Runs a Safari memory diagnostics session for the wasm app on studiomac and
brings the measurements back. Windows entry point; the Mac-side pieces are
scripts/wasm/serve.py (COOP/COEP server + telemetry sink) and
scripts/remote/safari-memtest.py (Safari driver + footprint sampler). See
docs/performance.md (headless wasm stress harness).

  scripts\remote\wasm-safari-memtest.ps1 -Mode stress -Preset standard
  scripts\remote\wasm-safari-memtest.ps1 -Mode soak -DurationMin 20 -Knobs 'PATCHY_WASM_MAX_MB=1024'
  scripts\remote\wasm-safari-memtest.ps1 -Mode interactive -DurationMin 30   # Seth drives, sampler records

Modes: stress (in-app --stress-test via the harness page), soak (memsoak.js
loop), idle (boot and sit), interactive (opens the real patchy.html, keeps
Safari and the server alive afterwards for hand testing).

Requires build\package\wasm-site staged by scripts\release\build-wasm.bat.
-Driver webdriver needs the one-time studiomac setup (sudo safaridriver
--enable plus Safari's Allow Remote Automation); the default open driver needs
nothing. Results land in build\memtest-results\<run-id>\.
#>
param(
  [ValidateSet('stress', 'soak', 'idle', 'interactive')][string]$Mode = 'idle',
  [ValidateSet('quick', 'small', 'standard', 'huge')][string]$Preset = 'quick',
  [string]$Knobs = '',
  [double]$DurationMin = 15,
  [ValidateSet('open', 'webdriver')][string]$Driver = 'open',
  [string]$SiteDir = '',
  [string]$Label = '',
  [switch]$SkipPush,
  [switch]$KeepServer
)

# 'Continue', not 'Stop': native stderr chatter (ssh/scp) must not become a
# terminating error; failures are handled via explicit LASTEXITCODE checks.
$ErrorActionPreference = 'Continue'

$remoteHost = 'seth@studiomac.local'
$port = 8993  # distinct from the 8973 dev server and testy's 8901

$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) { throw 'wasm-safari-memtest.ps1 must run inside the Patchy repository' }
$repoRoot = $repoRoot.Trim()
if (-not $SiteDir) { $SiteDir = Join-Path $repoRoot 'build\package\wasm-site' }
if (-not (Test-Path (Join-Path $SiteDir 'patchy.wasm'))) {
  throw "No staged site at $SiteDir - run scripts\release\build-wasm.bat first."
}

$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + $(if ($Label) { "-$Label" } else { "-$Mode" })
$remoteBase = 'patchy-memtest'
$remoteResults = "$remoteBase/results/$runId"
$localResults = Join-Path $repoRoot "build\memtest-results\$runId"

try {
  if (-not $SkipPush) {
    Write-Host "== pushing site + scripts to ${remoteHost}:~/$remoteBase =="
    ssh $remoteHost "mkdir -p ~/$remoteBase/site ~/$remoteResults"
    if ($LASTEXITCODE -ne 0) { throw 'ssh mkdir failed (is studiomac reachable?)' }
    # Identity files only: the .br/.gz variants halve the copy but serve.py
    # would then hand Safari brotli it may cache oddly; measurement runs favor
    # deterministic identity responses.
    # index.html only exists in staged sites; a raw build dir (variant builds
    # under build\wasm-<variant>) serves Qt's patchy.html alone. Push whatever
    # of the optional page files exist; patchy.wasm was already verified.
    $siteFiles = @('patchy.wasm', 'patchy.js', 'patchy.data', 'qtloader.js',
                   'patchy.html', 'index.html') |
      ForEach-Object { Join-Path $SiteDir $_ } | Where-Object { Test-Path $_ }
    $harness = Join-Path $SiteDir 'stress-harness.html'
    $soak = Join-Path $SiteDir 'memsoak.js'
    # Older stagings predate the harness: fall back to the repo copies, whose
    # unsubstituted cache-tag token makes the page use a per-load tag.
    if (-not (Test-Path $harness)) { $harness = Join-Path $repoRoot 'scripts\wasm\stress-harness.html' }
    if (-not (Test-Path $soak)) { $soak = Join-Path $repoRoot 'scripts\wasm\memsoak.js' }
    scp -q @siteFiles $harness $soak "${remoteHost}:$remoteBase/site/"
    if ($LASTEXITCODE -ne 0) { throw 'scp of the site failed' }
    scp -q (Join-Path $repoRoot 'scripts\wasm\serve.py') (Join-Path $repoRoot 'scripts\remote\safari-memtest.py') "${remoteHost}:$remoteBase/"
    if ($LASTEXITCODE -ne 0) { throw 'scp of the helper scripts failed' }
  }

  Write-Host "== starting serve.py on ${remoteHost}:$port =="
  $startServer = "pkill -f 'serve.py .*$port' 2>/dev/null; sleep 1; " +
    "nohup python3 ~/$remoteBase/serve.py ~/$remoteBase/site $port --results-dir ~/$remoteResults " +
    "> ~/$remoteBase/server.log 2>&1 & echo `$! > ~/$remoteBase/server.pid; sleep 1; " +
    "curl -s -o /dev/null -w '%{http_code}' http://localhost:$port/stress-harness.html"
  $probe = ssh $remoteHost $startServer
  if ($probe -notmatch '200') {
    ssh $remoteHost "cat ~/$remoteBase/server.log" | Write-Host
    throw "serve.py did not come up on port $port (probe: $probe)"
  }

  $page = if ($Mode -eq 'interactive') { 'patchy.html' } else { 'stress-harness.html' }
  # autostart=1: a driven run must boot even after a streak of kills tripped
  # the harness page's anti-hammer gate.
  $query = "PATCHY_MEM_LOG=1&autostart=1"
  if ($Mode -eq 'stress') { $query = "mode=stress&preset=$Preset&$query" }
  elseif ($Mode -eq 'soak') { $query = "mode=soak&$query" }
  elseif ($Mode -eq 'idle') { $query = "mode=idle&$query" }
  if ($Knobs) { $query = "$query&$Knobs" }
  $url = "http://localhost:$port/${page}?$query"

  Write-Host "== running $Mode for $DurationMin min: $url =="
  $keepSafari = if ($Mode -eq 'interactive') { ' --keep-safari' } else { '' }
  $runCmd = "python3 ~/$remoteBase/safari-memtest.py --url '$url' --driver $Driver " +
    "--duration-min $DurationMin --results-dir ~/$remoteResults " +
    "--telemetry-dir ~/$remoteResults$keepSafari"
  ssh $remoteHost $runCmd
  $runExit = $LASTEXITCODE

  Write-Host "== fetching results to $localResults =="
  New-Item -ItemType Directory -Force $localResults | Out-Null
  scp -q -r "${remoteHost}:$remoteResults/*" $localResults
  if (Test-Path (Join-Path $localResults 'summary.json')) {
    Write-Host "== summary =="
    Get-Content (Join-Path $localResults 'summary.json') | Write-Host
  }
  exit $runExit
}
finally {
  if (-not $KeepServer -and $Mode -ne 'interactive') {
    ssh $remoteHost "kill `$(cat ~/$remoteBase/server.pid 2>/dev/null) 2>/dev/null; rm -f ~/$remoteBase/server.pid" | Out-Null
    Write-Host "== server stopped =="
  } else {
    Write-Host "== server left running on ${remoteHost}:$port (stop: ssh $remoteHost 'kill `$(cat ~/$remoteBase/server.pid)') =="
  }
}

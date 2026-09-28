<#
One-time (idempotent) provisioning of a second Windows machine as an on-request
release build box for scripts\remote\remote-build.ps1 -Target windows. Run from the
dev box; the ssh account must be an administrator (Windows OpenSSH sessions for admins
are elevated). -Root picks where the work tree and tools go; it must match the
`workTree` (<Root>\src) in hosts.local.json.

  scp scripts/remote/setup-windows.ps1 <windows-build-host>:
  ssh <windows-build-host> "powershell -NoProfile -ExecutionPolicy Bypass -File setup-windows.ps1 -Root C:\patchy"

Installs, skipping anything already present:
  - VS 2026 Build Tools (MSVC x64, Windows SDK 10.0.26100, VS-bundled CMake + Ninja),
    the same toolset and SDK the dev box's release preset uses
  - Git for Windows (winget, machine scope), so pushes over ssh reach git receive-pack
  - Python 3.13 (winget, machine scope), which the release configure requires
  - the bare repo ~\patchy.git and the work tree <Root>\src (build output goes there)
Qt is not downloaded here: copying the dev box's .deps\Qt\6.8.3\msvc2022_64 guarantees
identical bits. The script prints the copy command when Qt is missing.
#>
param([string]$Root = 'C:\patchy')

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'   # PS 5.1 downloads crawl with the progress bar on
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$tools = Join-Path $Root 'tools'
$src = Join-Path $Root 'src'
$bare = Join-Path $env:USERPROFILE 'patchy.git'
$qtVer = '6.8.3'
$qtDir = Join-Path $src ".deps\Qt\$qtVer\msvc2022_64"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

Write-Host '== Patchy windows setup =='
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { Write-Error 'run elevated (ssh as an administrator account)'; exit 1 }
New-Item -ItemType Directory -Force $tools | Out-Null

function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Find-VcInstall {
  if (-not (Test-Path $vswhere)) { return $null }
  $p = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if ($p) { return $p.Trim() } else { return $null }
}

# --- 1. VS 2026 Build Tools ------------------------------------------------------------
$vs = Find-VcInstall
if (-not $vs) {
  Write-Host '== Installing VS 2026 Build Tools (several GB, 10-30 min) =='
  $boot = Join-Path $tools 'vs_BuildTools.exe'
  # The stable-channel link matches the dev box (channel VisualStudio.18.Release).
  # aka.ms/vs/18/release/... is NOT valid for VS 18: it redirects to Bing.
  Invoke-WebRequest -UseBasicParsing 'https://aka.ms/vs/18/stable/vs_buildtools.exe' -OutFile $boot
  $vsArgs = @('--quiet', '--wait', '--norestart', '--nocache',
    '--add', 'Microsoft.VisualStudio.Workload.VCTools',
    '--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    '--add', 'Microsoft.VisualStudio.Component.VC.CMake.Project',
    '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.26100',
    '--includeRecommended')
  $p = Start-Process -FilePath $boot -ArgumentList $vsArgs -Wait -PassThru
  # 3010 = success, reboot recommended.
  if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { Write-Error "Build Tools installer exited $($p.ExitCode)"; exit 1 }
  $vs = Find-VcInstall
  if (-not $vs) { Write-Error 'Build Tools installed but vswhere cannot find the VC tools'; exit 1 }
}
Write-Host "VS: $vs"

# --- 2. Git for Windows ----------------------------------------------------------------
Refresh-Path
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
  Write-Host '== Installing Git for Windows (winget) =='
  winget install --id Git.Git -e --scope machine --silent --accept-source-agreements --accept-package-agreements --disable-interactivity
  Refresh-Path
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Write-Error 'git still not on PATH after winget install'; exit 1 }
}

# --- 2b. Python 3 ----------------------------------------------------------------------
# The release configure requires a Python 3.8+ interpreter (translation checks; stdlib
# only). The WindowsApps python.exe is a Store alias that does not run, so look for a
# real install. 3.13 matches the dev box's interpreter.
function Find-Python {
  foreach ($c in @(Get-Command python -All -ErrorAction SilentlyContinue)) {
    if ($c.Source -notlike '*\WindowsApps\*') { return $c.Source }
  }
  return $null
}
if (-not (Find-Python)) {
  Write-Host '== Installing Python 3.13 (winget) =='
  winget install --id Python.Python.3.13 -e --scope machine --silent --accept-source-agreements --accept-package-agreements --disable-interactivity --override '/quiet InstallAllUsers=1 PrependPath=1 Include_test=0'
  Refresh-Path
  if (-not (Find-Python)) { Write-Error 'python still not on PATH after winget install'; exit 1 }
}

# --- 3. Bare repo + work tree ----------------------------------------------------------
if (-not (Test-Path (Join-Path $bare 'HEAD'))) { git init -q --bare $bare }
# init + remote add rather than clone: it also works when $src already exists (for
# example when Qt was copied into .deps first). remote-build.ps1 fetches and checks out.
if (-not (Test-Path (Join-Path $src '.git'))) {
  New-Item -ItemType Directory -Force $src | Out-Null
  git -C $src init -q
  git -C $src remote add origin $bare
}
# Elevated sessions can create files owned by BUILTIN\Administrators; keep git from
# refusing them as "dubious ownership".
$safe = @(git config --global --get-all safe.directory)
foreach ($d in @($bare, $src)) {
  $fwd = $d.Replace('\', '/')
  if ($safe -notcontains $fwd) { git config --global --add safe.directory $fwd }
}
git -C $src config core.longpaths true

# --- 4. Qt -----------------------------------------------------------------------------
$qtOk = Test-Path (Join-Path $qtDir 'bin\Qt6Core.dll')
if (-not $qtOk) {
  New-Item -ItemType Directory -Force (Split-Path $qtDir) | Out-Null
  Write-Host "Qt $qtVer msvc2022_64 is missing. From the dev box (Git Bash), copy it over:"
  Write-Host "  tar -cf - -C <main-checkout>/.deps/Qt/$qtVer msvc2022_64 | ssh <windows-build-host> `"tar -xf - -C $((Split-Path $qtDir).Replace('\', '/'))`""
}

# --- 5. Versions -----------------------------------------------------------------------
Write-Host '== Versions =='
$probe = Join-Path $tools 'versions.cmd'
@"
@echo off
call "$vs\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
cl 2>&1 | findstr /C:"Version"
cmake --version | findstr /B cmake
for /f %%v in ('ninja --version') do echo ninja %%v
"@ | Set-Content -Encoding Ascii $probe
cmd /c $probe
git --version
& (Find-Python) --version
if ($qtOk) { Write-Host "Qt: $qtDir OK" } else { Write-Host 'Qt: MISSING (see copy command above)' }
Write-Host '== windows setup complete =='

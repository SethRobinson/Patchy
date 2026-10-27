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
    the same toolset and SDK the dev box's release preset uses; an ARM64 host also gets
    the native ARM64 MSVC tools
  - Git for Windows (winget, machine scope; an older Git than 2.35 is upgraded), so pushes
    over ssh reach git receive-pack; a broken winget source (0x8a15000f) is repaired first
  - Python 3.13 (winget, machine scope), which the release configure requires
  - the bare repo ~\patchy.git and the work tree <Root>\src (build output goes there)
Qt is not downloaded on an x64 host: copying the dev box's .deps\Qt\6.8.3\msvc2022_64
guarantees identical bits, and the script prints the copy command when Qt is missing.
An ARM64 host gets the native msvc2022_arm64 kit through aqtinstall instead (the dev box
has none to copy). Qt 6.8.3 ships no qtpdf add-on for it, so PDF import builds as the stub.
#>
param([string]$Root = 'C:\patchy')

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'   # PS 5.1 downloads crawl with the progress bar on
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$tools = Join-Path $Root 'tools'
$src = Join-Path $Root 'src'
$bare = Join-Path $env:USERPROFILE 'patchy.git'
$qtVer = '6.8.3'
# The ssh session runs 64-bit Windows PowerShell, which reports the native architecture.
$arm64 = $env:PROCESSOR_ARCHITECTURE -eq 'ARM64'
$qtArch = if ($arm64) { 'msvc2022_arm64' } else { 'msvc2022_64' }
$vcTools = if ($arm64) { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' } else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
$qtDir = Join-Path $src ".deps\Qt\$qtVer\$qtArch"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

Write-Host '== Patchy windows setup =='
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) { Write-Error 'run elevated (ssh as an administrator account)'; exit 1 }
New-Item -ItemType Directory -Force $tools | Out-Null

function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

# winget in an ssh session can fail every search with 0x8a15000f ("Data required by the
# source is missing") because the account has no Microsoft.Winget.Source package, and
# `winget source reset` does not bring it back. Install the signed index package winget
# itself downloads, once, before the first winget install.
$script:wingetChecked = $false
function Repair-WingetSource {
  if ($script:wingetChecked) { return }
  $script:wingetChecked = $true
  winget search --id Git.Git --source winget --accept-source-agreements --disable-interactivity *> $null
  if ($LASTEXITCODE -eq 0) { return }
  Write-Host '== winget source is broken: installing its source package =='
  $msix = Join-Path $tools 'winget-source2.msix'
  Invoke-WebRequest -UseBasicParsing 'https://cdn.winget.microsoft.com/cache/source2.msix' -OutFile $msix
  $sig = Get-AuthenticodeSignature $msix
  if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    Write-Error "winget source package signature is $($sig.Status) ($($sig.SignerCertificate.Subject)); not installing it"; exit 1
  }
  Add-AppxPackage -Path $msix
}

function Find-VcInstall {
  if (-not (Test-Path $vswhere)) { return $null }
  # -version 18.0 (a minimum): an older VS with VC tools (2019, 2022) must not count as
  # installed, or the VS 2026 Build Tools install is skipped and the build uses the old toolset.
  $p = & $vswhere -latest -products * -version 18.0 -requires $vcTools -property installationPath
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
    '--add', $vcTools,
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
# An existing Git older than 2.35 is upgraded too: those keep git-receive-pack out of
# Git\cmd (remote-snapshot.ps1 copes, but other tools that push over ssh do not).
$gitOld = $false
if (Get-Command git -ErrorAction SilentlyContinue) {
  $gv = [regex]::Match((git --version), '(\d+)\.(\d+)')
  $gitOld = $gv.Success -and ([version]"$($gv.Groups[1].Value).$($gv.Groups[2].Value)" -lt [version]'2.35')
}
if ($gitOld -or -not (Get-Command git -ErrorAction SilentlyContinue)) {
  Write-Host '== Installing Git for Windows (winget) =='
  Repair-WingetSource
  winget install --id Git.Git -e --scope machine --force --silent --accept-source-agreements --accept-package-agreements --disable-interactivity
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
  # CMake's FindPython3 also reads the PEP 514 registry entries, so a registered 3.8+ install
  # that is not on PATH is enough, and saves a winget run (winget's source can fail in an
  # ssh session on older Windows builds: 0x8a15000f).
  foreach ($root in 'HKLM:\SOFTWARE\Python\PythonCore', 'HKCU:\SOFTWARE\Python\PythonCore') {
    foreach ($k in @(Get-ChildItem $root -ErrorAction SilentlyContinue)) {
      $exe = (Get-ItemProperty (Join-Path $k.PSPath 'InstallPath') -ErrorAction SilentlyContinue).ExecutablePath
      $v = $k.PSChildName -as [version]
      if ($exe -and (Test-Path $exe) -and $v -and $v -ge [version]'3.8') { return $exe }
    }
  }
  return $null
}
if (-not (Find-Python)) {
  Write-Host '== Installing Python 3.13 (winget) =='
  Repair-WingetSource
  winget install --id Python.Python.3.13 -e --scope machine --silent --accept-source-agreements --accept-package-agreements --disable-interactivity --override '/quiet InstallAllUsers=1 PrependPath=1 Include_test=0'
  Refresh-Path
  if (-not (Find-Python)) { Write-Error 'no Python 3.8+ after the winget install: install Python 3.13 for all users by hand and rerun'; exit 1 }
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
if (-not $qtOk -and $arm64) {
  Write-Host "== Installing Qt $qtVer $qtArch (aqtinstall) =="
  $aqtVenv = Join-Path $tools 'aqt-venv'
  $aqt = Join-Path $aqtVenv 'Scripts\aqt.exe'
  if (-not (Test-Path $aqt)) {
    & (Find-Python) -m venv $aqtVenv
    & (Join-Path $aqtVenv 'Scripts\python.exe') -m pip install --quiet aqtinstall
  }
  & $aqt install-qt windows_arm64 desktop $qtVer "win64_$qtArch" -m qtimageformats -O (Join-Path $src '.deps\Qt')
  $qtOk = Test-Path (Join-Path $qtDir 'bin\Qt6Core.dll')
  if (-not $qtOk) { Write-Error "aqt install-qt did not produce $qtDir"; exit 1 }
}
if (-not $qtOk) {
  New-Item -ItemType Directory -Force (Split-Path $qtDir) | Out-Null
  Write-Host "Qt $qtVer $qtArch is missing. From the dev box (Git Bash), copy it over:"
  Write-Host "  tar -cf - -C <main-checkout>/.deps/Qt/$qtVer $qtArch | ssh <windows-build-host> `"tar -xf - -C $((Split-Path $qtDir).Replace('\', '/'))`""
}

# --- 5. Versions -----------------------------------------------------------------------
Write-Host '== Versions =='
$probe = Join-Path $tools 'versions.cmd'
$vsArch = if ($arm64) { 'arm64' } else { 'x64' }
@"
@echo off
call "$vs\Common7\Tools\VsDevCmd.bat" -arch=$vsArch -host_arch=$vsArch >nul
cl 2>&1 | findstr /C:"Version"
cmake --version | findstr /B cmake
for /f %%v in ('ninja --version') do echo ninja %%v
"@ | Set-Content -Encoding Ascii $probe
cmd /c $probe
git --version
& (Find-Python) --version
if ($qtOk) { Write-Host "Qt: $qtDir OK" } else { Write-Host 'Qt: MISSING (see copy command above)' }
Write-Host '== windows setup complete =='

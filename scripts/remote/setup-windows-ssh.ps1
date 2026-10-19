#Requires -Version 5.1
<#
Step zero for a new Windows offload build host: turns on the OpenSSH server so the dev
box can reach it; setup-windows.ps1 (run over that ssh connection) does the rest. It
runs ON the build host, because nothing can reach the machine remotely yet.

Launch it with setup-windows-ssh.cmd beside it (double-click it, or run it from cmd or
PowerShell; the result is the same). The launcher always starts 64-bit Windows
PowerShell 5.1, and this script relaunches itself elevated through UAC. It refuses
to run in PowerShell 7, a 32-bit PowerShell, or from a network path, and it never
needs to be typed into a particular shell.

Idempotent. It:
  - installs the OpenSSH.Server capability, starts sshd, sets it to start automatically
  - sets the ssh default shell to 64-bit Windows PowerShell 5.1 (Windows OpenSSH starts
    cmd.exe otherwise, and remote-build.ps1 sends PowerShell 5.1 commands)
  - allows TCP 22 from the local subnet only, on every network profile
  - appends the dev box key (the one *.pub beside this script, or -PublicKeyFile) to
    administrators_authorized_keys with the ACL sshd requires, so the ssh session of
    any administrator account is elevated, as setup-windows.ps1 requires
  - verifies each of those, prints host facts (accounts, CPU, memory, free disk, Smart
    App Control state; see docs/building.md), and ends with PASS or FAIL

Everything is also written to setup-windows-ssh.log beside the script, so the dev box
can read the result over a file share. Exit code 0 means PASS.
#>
param(
  [string]$PublicKeyFile = '',
  # Set by the launcher and by the elevated relaunch: wait for Enter before the window closes.
  [switch]$Pause
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

function Finish([int]$code) {
  if ($Pause) {
    # Drop keys typed while the script ran, so a stray Enter cannot close the window unseen.
    try { $Host.UI.RawUI.FlushInputBuffer() } catch {}
    Read-Host 'Press Enter to close' | Out-Null
  }
  exit $code
}

# --- Host checks, before anything is changed ---------------------------------------------
if ($PSVersionTable.PSEdition -ne 'Desktop' -or $PSVersionTable.PSVersion.Major -ne 5) {
  Write-Host "This is PowerShell $($PSVersionTable.PSVersion) ($($PSVersionTable.PSEdition)). It must be Windows PowerShell 5.1: run setup-windows-ssh.cmd instead." -ForegroundColor Red
  Finish 2
}
if (-not [Environment]::Is64BitProcess) {
  Write-Host 'This is a 32-bit PowerShell, which cannot manage the 64-bit OpenSSH install. Run setup-windows-ssh.cmd instead.' -ForegroundColor Red
  Finish 2
}
if ($here.StartsWith('\\')) {
  Write-Host "The script is on a network path ($here). An elevated process may not see the share: copy the folder to a local disk and run it there." -ForegroundColor Red
  Finish 2
}

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  Write-Host 'Not elevated: asking UAC to run this again as administrator (a new window opens).'
  $relaunch = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Pause')
  if ($PublicKeyFile) { $relaunch += @('-PublicKeyFile', "`"$PublicKeyFile`"") }
  try {
    $p = Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList $relaunch -Verb RunAs -Wait -PassThru
  } catch {
    Write-Host "UAC elevation was declined or failed: $($_.Exception.Message)" -ForegroundColor Red
    Finish 3
  }
  # Repeat the elevated window's result here, so it stays on screen even when that window
  # is gone (it closes on Enter, and a key pressed during the long install can be that Enter).
  $logPath = Join-Path $here 'setup-windows-ssh.log'
  if (Test-Path $logPath) {
    $lines = @(Get-Content -Path $logPath)
    $stars = @(for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^\*{10,}$') { $i } })
    if ($stars.Count -ge 3 -and $stars[2] -gt $stars[1] + 1) { $lines = $lines[($stars[1] + 1)..($stars[2] - 1)] }
    Write-Host ''
    Write-Host '== result of the elevated run =='
    foreach ($line in $lines) {
      $color = if ($line -match '\bFAIL\b|^ERROR') { 'Red' } elseif ($line -match '== PASS') { 'Green' } else { (Get-Host).UI.RawUI.ForegroundColor }
      Write-Host $line -ForegroundColor $color
    }
  }
  Write-Host "Elevated run finished with exit code $($p.ExitCode); full log in $logPath."
  Finish $p.ExitCode
}

# --- Elevated from here on ----------------------------------------------------------------
$log = Join-Path $here 'setup-windows-ssh.log'
Start-Transcript -Path $log -Force | Out-Null
$failures = New-Object System.Collections.Generic.List[string]
function Check([string]$what, [bool]$ok) {
  if ($ok) { Write-Host "  ok    $what" } else { Write-Host "  FAIL  $what" -ForegroundColor Red; $failures.Add($what) }
}

try {
  if (-not $PublicKeyFile) {
    $pubs = @(Get-ChildItem -Path $here -Filter '*.pub' -File)
    if ($pubs.Count -ne 1) { throw "expected exactly one *.pub file beside the script, found $($pubs.Count); pass -PublicKeyFile" }
    $PublicKeyFile = $pubs[0].FullName
  }
  $key = (Get-Content -Raw -Path $PublicKeyFile).Trim()
  if ($key -notmatch '^ssh-(ed25519|rsa) \S+') { throw "$PublicKeyFile does not hold an OpenSSH public key" }
  Write-Host "key: $PublicKeyFile"

  Write-Host '== OpenSSH server =='
  $cap = Get-WindowsCapability -Online -Name 'OpenSSH.Server*' | Select-Object -First 1
  if (-not $cap) { throw 'Windows reports no OpenSSH.Server capability' }
  if ($cap.State -ne 'Installed') {
    Write-Host "installing $($cap.Name) (it can take 15 minutes and needs Windows Update to be reachable; leave this window alone)"
    Add-WindowsCapability -Online -Name $cap.Name | Out-Null
  }
  Set-Service -Name sshd -StartupType Automatic
  Start-Service -Name sshd   # the first start also writes the default sshd_config

  Write-Host '== default shell: 64-bit Windows PowerShell 5.1 =='
  $shell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
  if (-not (Test-Path 'HKLM:\SOFTWARE\OpenSSH')) { New-Item -Path 'HKLM:\SOFTWARE\OpenSSH' -Force | Out-Null }
  New-ItemProperty -Path 'HKLM:\SOFTWARE\OpenSSH' -Name DefaultShell -Value $shell -PropertyType String -Force | Out-Null

  Write-Host '== firewall: TCP 22 from the local subnet =='
  $ruleName = 'OpenSSH-Server-In-TCP'
  if (-not (Get-NetFirewallRule -Name $ruleName -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name $ruleName -DisplayName 'OpenSSH Server (sshd)' -Direction Inbound -Protocol TCP -LocalPort 22 -Action Allow | Out-Null
  }
  # Any profile, because a home network is often classified Public; LocalSubnet keeps it LAN-only.
  Set-NetFirewallRule -Name $ruleName -Enabled True -Profile Any -RemoteAddress LocalSubnet

  Write-Host '== administrators_authorized_keys =='
  $auth = Join-Path $env:ProgramData 'ssh\administrators_authorized_keys'
  $existing = @()
  if (Test-Path $auth) { $existing = @(Get-Content -Path $auth | ForEach-Object { $_.Trim() }) }
  if ($existing -notcontains $key) { Add-Content -Path $auth -Value $key -Encoding Ascii }
  # sshd silently ignores the file unless only SYSTEM and Administrators can access it.
  & icacls.exe $auth /inheritance:r /grant '*S-1-5-32-544:F' /grant '*S-1-5-18:F' | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "icacls exited $LASTEXITCODE on $auth" }

  # No sshd restart: DefaultShell is read for each new session, and a restart would drop
  # the session running this when it is re-run over ssh to repair an earlier setup.

  Write-Host '== verify =='
  Check 'sshd is running' ((Get-Service sshd).Status -eq 'Running')
  Check 'sshd starts automatically' ((Get-Service sshd).StartType -eq 'Automatic')
  Check 'sshd listens on TCP 22' ($null -ne (Get-NetTCPConnection -LocalPort 22 -State Listen -ErrorAction SilentlyContinue))
  Check "default shell is $shell" ((Get-ItemProperty 'HKLM:\SOFTWARE\OpenSSH').DefaultShell -eq $shell)
  $rule = Get-NetFirewallRule -Name $ruleName
  Check 'firewall rule enabled for every profile' ($rule.Enabled -eq 'True' -and $rule.Action -eq 'Allow' -and "$($rule.Profile)" -eq 'Any')
  Check 'dev box key is authorized' (@(Get-Content -Path $auth | ForEach-Object { $_.Trim() }) -contains $key)
  $acl = (Get-Acl -Path $auth).Access | ForEach-Object { $_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value }
  Check 'key file ACL is SYSTEM and Administrators only' (@($acl | Where-Object { $_ -ne 'S-1-5-18' -and $_ -ne 'S-1-5-32-544' }).Count -eq 0)
  $cfg = Join-Path $env:ProgramData 'ssh\sshd_config'
  Check 'sshd_config reads administrators_authorized_keys for admins' ((Test-Path $cfg) -and [bool](Select-String -Path $cfg -Pattern '^\s*Match Group administrators' -Quiet))

  Write-Host '== host facts =='
  Write-Host "computer: $env:COMPUTERNAME   this account: $env:USERNAME"
  try {
    $admins = Get-LocalGroupMember -SID 'S-1-5-32-544' | ForEach-Object { $_.Name }
    Write-Host ('administrators: ' + ($admins -join ', '))
  } catch { Write-Host "administrators: (could not list: $($_.Exception.Message))" }
  $cs = Get-CimInstance Win32_ComputerSystem
  $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
  Write-Host "cpu: $($cpu.Name.Trim()), $($cs.NumberOfLogicalProcessors) threads"
  Write-Host ('memory: {0:N0} GB' -f ($cs.TotalPhysicalMemory / 1GB))
  Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' | ForEach-Object {
    Write-Host ('drive {0} {1:N0} GB free of {2:N0} GB' -f $_.DeviceID, ($_.FreeSpace / 1GB), ($_.Size / 1GB))
  }
  try { Write-Host "Smart App Control: $((Get-MpComputerStatus).SmartAppControlState)" } catch { Write-Host 'Smart App Control: unknown' }
  $os = Get-CimInstance Win32_OperatingSystem
  Write-Host "os: $($os.Caption) $($os.Version)"
} catch {
  Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
  $failures.Add("error: $($_.Exception.Message)")
}

if ($failures.Count -eq 0) {
  Write-Host '== PASS: the dev box can now ssh in with the matching private key ==' -ForegroundColor Green
  $code = 0
} else {
  Write-Host "== FAIL: $($failures.Count) problem(s) above ==" -ForegroundColor Red
  $code = 1
}
Stop-Transcript | Out-Null
Finish $code

param([string]$Python, [string]$RunDir, [string]$User, [string]$TaskName, [switch]$Stop)
# Starts testy\photoshop_agent.py in $User's logged-on console session through a
# temporary scheduled task (COM cannot launch Photoshop from an ssh session), or with
# -Stop removes that task once the agent has exited. Run over ssh by
# testy\drivers\photoshop_remote.py; every machine-specific value comes from its
# arguments (config.local.json), none is stored here.
$ErrorActionPreference = 'Stop'
if ($Stop) {
  Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
  'removed'
  exit 0
}
# pythonw keeps a console window off the host's desktop.
$exe = Join-Path (Split-Path -Parent $Python) 'pythonw.exe'
if (-not (Test-Path -LiteralPath $exe)) { $exe = $Python }
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
$action = New-ScheduledTaskAction -Execute $exe `
  -Argument "`"$RunDir\agent\photoshop_agent.py`" --dir `"$RunDir`"" -WorkingDirectory "$RunDir\agent"
$principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
  -ExecutionTimeLimit (New-TimeSpan -Hours 72)
Register-ScheduledTask -TaskName $TaskName -Action $action -Principal $principal -Settings $settings | Out-Null
Start-ScheduledTask -TaskName $TaskName
'started'

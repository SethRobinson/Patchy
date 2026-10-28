param(
  [Parameter(Mandatory)][string]$Python,     # python.exe with pywin32, Pillow, numpy
  [Parameter(Mandatory)][string]$Arguments,  # testy.py's arguments, quoted as on a command line
  [Parameter(Mandatory)][string]$User,       # the user logged on at the console
  [string]$Log = '',                         # default: testy\runs\console-run.log
  [int]$TimeoutMinutes = 720,
  [string]$TaskName = 'testy-console-run',
  [switch]$NoWait)
# Runs testy\testy.py in $User's logged-on console session through a temporary scheduled
# task, for a machine reached over ssh: COM cannot start Photoshop from an ssh session
# (CO_E_SERVER_EXEC_FAILURE), only from the interactive desktop. Waits for the run, prints
# its log, exits with its exit code and removes the task; -NoWait returns once it started
# (run again with the same -TaskName later to wait; the task is removed by whoever waits).
# Pass --no-browser --exit-when-done in $Arguments: nobody can see a dashboard. Keep the server
# (no --no-serve) when Photopea runs: it fetches staged files and uploads exports through it.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path -LiteralPath (Join-Path $repo 'testy\testy.py'))) { throw "no testy\testy.py under $repo" }
if (-not $Log) { $Log = Join-Path $repo 'testy\runs\console-run.log' }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Log) | Out-Null
$existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if (-not $existing) {
  # A small .cmd beside the log carries the arguments and the redirection, so they are
  # quoted exactly once (a literal % in them must be written %%).
  $cmdFile = [IO.Path]::ChangeExtension($Log, '.cmd')
  # UTF-8 output: a localized Photoshop's alerts do not fit the console code page.
  @('@set PYTHONUTF8=1',
    "@`"$Python`" `"$repo\testy\testy.py`" $Arguments > `"$Log`" 2>&1",
    '@set TESTY_EXIT=%ERRORLEVEL%',
    "@echo exit %TESTY_EXIT% >> `"$Log`"",
    '@exit %TESTY_EXIT%') | Set-Content -LiteralPath $cmdFile -Encoding Ascii
  $action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument "/c `"$cmdFile`"" -WorkingDirectory $repo
  $principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive -RunLevel Limited
  $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Minutes $TimeoutMinutes)
  Register-ScheduledTask -TaskName $TaskName -Action $action -Principal $principal -Settings $settings | Out-Null
  Remove-Item -LiteralPath $Log -ErrorAction SilentlyContinue
  Start-ScheduledTask -TaskName $TaskName
  "started $TaskName; log: $Log"
  if ($NoWait) { exit 0 }
  Start-Sleep -Seconds 3
}
$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
while ((Get-ScheduledTask -TaskName $TaskName).State -eq 'Running' -and (Get-Date) -lt $deadline) {
  Start-Sleep -Seconds 10
}
$code = (Get-ScheduledTaskInfo -TaskName $TaskName).LastTaskResult
if ((Get-ScheduledTask -TaskName $TaskName).State -eq 'Running') { "timed out after $TimeoutMinutes minutes; the task keeps running"; exit 124 }
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
if (Test-Path -LiteralPath $Log) { Get-Content -LiteralPath $Log -Tail 60 -Encoding UTF8 } else { 'no log was written' }
"task result: $code"
exit $code

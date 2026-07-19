@echo off
rem Testy control panel: kills stale Testy servers/runs, starts the dashboard server,
rem and opens the panel in the browser. Runs are started from the panel itself.
rem Machine settings (python path, port, corpus, editor paths) live in
rem config.local.json next to this file; see config.example.json.
cd /d "%~dp0"
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='python.exe'\" | Where-Object { $_.CommandLine -match 'testy[\\/](serve|testy)\.py|serve\.py [0-9]+$' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"
for /f "usebackq delims=" %%p in (`powershell -NoProfile -Command "$c = if (Test-Path 'config.local.json') { (Get-Content 'config.local.json' -Raw | ConvertFrom-Json) }; if ($c -and $c.python) { $c.python } else { 'python' }"`) do set "TESTY_PYTHON=%%p"
for /f "usebackq delims=" %%q in (`powershell -NoProfile -Command "$c = if (Test-Path 'config.local.json') { (Get-Content 'config.local.json' -Raw | ConvertFrom-Json) }; if ($c -and $c.port) { $c.port } else { 8901 }"`) do set "TESTY_PORT=%%q"
start "Testy server" /min "%TESTY_PYTHON%" "%~dp0serve.py" %TESTY_PORT%
timeout /t 2 /nobreak >nul
start "" http://127.0.0.1:%TESTY_PORT%/

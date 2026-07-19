@echo off
rem Testy control panel: kills stale Testy servers/runs, starts the dashboard server,
rem and opens the panel in the browser. Runs are started from the panel itself.
cd /d "%~dp0"
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='python.exe'\" | Where-Object { $_.CommandLine -match 'Patchy.testy' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"
start "Testy server" /min "C:\Users\Seth\miniconda3\python.exe" serve.py 8901
timeout /t 2 /nobreak >nul
start "" http://127.0.0.1:8901/

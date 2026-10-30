@echo off
rem Double-clickable wrapper: builds the web release on the remote wasm build host
rem (snapshot, wasm-release site, full wasm core suite) and copies build\package\wasm-site
rem back, with live output in this window. See scripts\remote\release-wasm.ps1.
cd /d "%~dp0..\.."
powershell -ExecutionPolicy Bypass -File "scripts\remote\release-wasm.ps1"
echo.
if not defined NO_PAUSE pause

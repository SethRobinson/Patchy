@echo off
rem Non-interactive release driver for agents and unattended runs. Same four builders
rem as release-all.bat, but every console runs release-worker.bat, which sets the
rem environment the batch files need (NO_PAUSE=1 so sign.bat and the upload scripts
rem never wait on a key, CMAKE_BUILD_PARALLEL_LEVEL=20 so every local cmake --build stays
rem throttled, and a Windows PowerShell 5.1 PSModulePath so the inline powershell
rem one-liners load the right modules even when launched from pwsh 7), logs the
rem target to build\release-logs\<target>.log, and writes exit=<code> to
rem build\release-logs\<target>.exit when it ends. Wait for the .exit files; never
rem wait for the consoles. Targets: windows mac linux wasm upload-wasm upload-all
rem upload-github (the GitHub Releases publish; needs the release commit pushed first).
rem With no arguments it starts the four builders; name targets to run a subset,
rem e.g. release-all-automated.bat windows wasm, or release-all-automated.bat upload-wasm.
rem Always go through this script (or release-worker.bat) for an unattended run: a
rem builder launched by hand without NO_PAUSE stops on sign.bat's pause with no exit
rem marker, which reads as a hung build. See docs/release-process.md.
setlocal EnableExtensions
cd /d "%~dp0..\.."
if not exist build\release-logs mkdir build\release-logs
if "%~1"=="" (
  call :launch windows
  call :launch mac
  call :launch linux
  call :launch wasm
) else (
  for %%T in (%*) do call :launch %%T
)
echo Targets launched. Logs and exit markers: build\release-logs\^<target^>.log and .exit
exit /b 0

:launch
if exist "build\release-logs\%~1.exit" del "build\release-logs\%~1.exit"
start "Patchy release - %~1" /min /belownormal cmd /c ""%~dp0release-worker.bat" %~1"
exit /b 0

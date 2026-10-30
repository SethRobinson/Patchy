@echo off
rem Non-interactive release driver for agents and unattended runs. Same four builders
rem as release-all.bat, but every console runs release-worker.bat, which sets the
rem environment the batch files need (NO_PAUSE=1 so sign.bat and the upload scripts
rem never wait on a key, CMAKE_BUILD_PARALLEL_LEVEL so every local cmake --build stays
rem throttled, and a Windows PowerShell 5.1 PSModulePath so the inline powershell
rem one-liners load the right modules even when launched from pwsh 7), logs the
rem target to build\release-logs\<target>.log, and writes exit=<code> to
rem build\release-logs\<target>.exit when it ends. Wait for the .exit files; never
rem wait for the consoles. Targets: windows (x64 and the cross-built ARM64 packages) mac
rem linux wasm (built and suite-tested on the remote wasm host, scripts\remote\release-wasm.ps1)
rem wasm-local (build-wasm.bat on this machine, no suite), verify-windows verify-windows-arm64 (the ARM64 package on the ARM64 host)
rem verify-mac verify-linux (the release tests, after the builders), upload-wasm upload-all
rem upload-desktop (upload-all without the wasm site), and upload-github (publish after
rem pushing the build commit with the OLD update manifest).
rem All requested targets launch in parallel. Run builds, upload-all, and upload-github
rem in separate invocations, waiting for successful exit markers between phases.
rem Only after the public release downloads are verified, update latest_version.json
rem and commit/push that announcement separately. Keep the tag on the build commit.
rem With no arguments it starts the four builders; name targets to run a subset,
rem e.g. release-all-automated.bat windows wasm, or release-all-automated.bat upload-wasm.
rem Always go through this script (or release-worker.bat) for an unattended run: a
rem builder launched by hand without NO_PAUSE stops on sign.bat's pause with no exit
rem marker, which reads as a hung build. See docs/release-process.md.
setlocal EnableExtensions
cd /d "%~dp0..\.."
if not exist build\release-logs mkdir build\release-logs
rem Preflight: the Linux metainfo (screenshot URLs pinned to a release tag, the <release>
rem entry) must describe the version in CMakeLists.txt before anything is built or
rem uploaded. 1.17 and 1.18 shipped with the screenshots still pinned to v1.16 because
rem nothing checked (docs/release-process.md, "Release safety checks").
python "%~dp0check-metainfo.py"
if not "%ERRORLEVEL%"=="0" goto metainfo_failed
rem Workers start at Idle priority, and two local builds split the job budget: the Windows
rem packager and a local wasm build at 20 jobs each, plus an LTCG link, froze the desktop
rem for 20 seconds on release day (1.23). The workers inherit PATCHY_RELEASE_JOBS.
set "PATCHY_RELEASE_JOBS="
set "LOCAL_WINDOWS="
set "LOCAL_WASM="
for %%T in (%*) do (
  if /i "%%~T"=="windows" set "LOCAL_WINDOWS=1"
  if /i "%%~T"=="wasm-local" set "LOCAL_WASM=1"
)
if defined LOCAL_WINDOWS if defined LOCAL_WASM set "PATCHY_RELEASE_JOBS=10"
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

:metainfo_failed
echo.
echo RELEASE PREFLIGHT FAILED: packaging\linux\com.rtsoft.patchy.metainfo.xml does not describe
echo the version in CMakeLists.txt ^(see the errors above^). Nothing was launched. Fix it with
echo   python scripts\release\check-metainfo.py --fix
echo and rerun. Python 3 must be on PATH.
exit /b 1

:launch
if exist "build\release-logs\%~1.exit" del "build\release-logs\%~1.exit"
start "Patchy release - %~1" /min /low cmd /c ""%~dp0release-worker.bat" %~1"
exit /b 0

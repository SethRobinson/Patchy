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
rem (the Flatpak repository and the wasm site on rtsoft.com) upload-desktop (the Flatpak
rem repository only), and upload-github (publish after
rem pushing the build commit with the OLD update manifest).
rem Before the version bump (docs/release-process.md, "Before the version bump"):
rem warncheck-windows warncheck-windows-arm64 warncheck-mac warncheck-linux warncheck-wasm
rem (the warning check on each build host; warncheck launches all five) and rehearse-windows
rem rehearse-windows-arm64 rehearse-mac rehearse-linux (the warning check, then both full
rem suites on that host; rehearse launches all four plus warncheck-wasm). They build no
rem package and need no bump, so a launch of only these skips the metainfo preflight.
rem All requested targets launch in parallel. Run builds, upload-all, and upload-github
rem in separate invocations, waiting for successful exit markers between phases.
rem Only after the public release downloads are verified, update latest_version.json
rem and commit/push that announcement separately. Keep the tag on the build commit.
rem With no arguments it starts the four builders; name targets to run a subset,
rem e.g. release-all-automated.bat windows wasm, or release-all-automated.bat upload-wasm.
rem Always go through this script (or release-worker.bat) for an unattended run: a
rem builder launched by hand without NO_PAUSE stops on sign.bat's pause with no exit
rem marker, which reads as a hung build. See docs/release-process-scripts.md.
setlocal EnableExtensions
cd /d "%~dp0..\.."
if not exist build\release-logs mkdir build\release-logs
rem The warning check and the rehearsal run between releases, when the metainfo still names
rem the next tag (check-metainfo.py reports the pending bump), so only a launch that holds
rem nothing else keeps the preflight off.
set "EARLY_ONLY="
if not "%~1"=="" set "EARLY_ONLY=1"
for %%T in (%*) do call :classify %%~T
if defined EARLY_ONLY goto ssh_preflight
rem Preflight: the Linux metainfo (screenshot URLs pinned to a release tag, the <release>
rem entry) must describe the version in CMakeLists.txt before anything is built or
rem uploaded. 1.17 and 1.18 shipped with the screenshots still pinned to v1.16 because
rem nothing checked (docs/release-process.md, "Release safety checks").
python "%~dp0check-metainfo.py"
if not "%ERRORLEVEL%"=="0" goto metainfo_failed
:ssh_preflight
rem Every Windows ssh call in scripts\ must pass -n (or feed stdin): an ssh that keeps the
rem worker's stdin can stay alive after its remote command ends, and the release driver hung
rem that way on five release days, each time on a call the earlier fixes had not touched.
python "%~dp0check-ssh-stdin.py"
if not "%ERRORLEVEL%"=="0" goto ssh_check_failed
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
  for %%T in (%*) do call :target %%T
)
echo Targets launched. Logs and exit markers: build\release-logs\^<target^>.log and .exit
exit /b 0

:classify
set "EARLY_NAME=%~1"
if /i "%EARLY_NAME%"=="warncheck" exit /b 0
if /i "%EARLY_NAME%"=="rehearse" exit /b 0
if /i "%EARLY_NAME:~0,10%"=="warncheck-" exit /b 0
if /i "%EARLY_NAME:~0,9%"=="rehearse-" exit /b 0
set "EARLY_ONLY="
exit /b 0

:target
if /i "%~1"=="warncheck" (
  call :launch warncheck-windows
  call :launch warncheck-windows-arm64
  call :launch warncheck-mac
  call :launch warncheck-linux
  call :launch warncheck-wasm
  exit /b 0
)
if /i "%~1"=="rehearse" (
  call :launch rehearse-windows
  call :launch rehearse-windows-arm64
  call :launch rehearse-mac
  call :launch rehearse-linux
  call :launch warncheck-wasm
  exit /b 0
)
call :launch %~1
exit /b 0

:metainfo_failed
echo.
echo RELEASE PREFLIGHT FAILED: packaging\linux\com.rtsoft.patchy.metainfo.xml does not describe
echo the version in CMakeLists.txt ^(see the errors above^). Nothing was launched. Fix it with
echo   python scripts\release\check-metainfo.py --fix
echo and rerun. Python 3 must be on PATH.
exit /b 1

:ssh_check_failed
echo.
echo RELEASE PREFLIGHT FAILED: an ssh call in scripts\ keeps its stdin ^(see the errors above^).
echo Add -n to it ^(or a ssh-stdin-ok comment with the reason^) and rerun. Nothing was launched.
exit /b 1

:launch
if exist "build\release-logs\%~1.exit" del "build\release-logs\%~1.exit"
start "Patchy release - %~1" /min /low cmd /c ""%~dp0release-worker.bat" %~1"
exit /b 0

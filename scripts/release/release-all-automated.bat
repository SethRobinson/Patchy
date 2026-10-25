@echo off
rem Non-interactive release driver for agents and unattended runs. Same four builders
rem as release-all.bat, but every console runs release-worker.bat, which sets the
rem environment the batch files need (NO_PAUSE=1 so sign.bat and the upload scripts
rem never wait on a key, CMAKE_BUILD_PARALLEL_LEVEL=20 so every local cmake --build stays
rem throttled, and a Windows PowerShell 5.1 PSModulePath so the inline powershell
rem one-liners load the right modules even when launched from pwsh 7), logs the
rem target to build\release-logs\<target>.log, and writes exit=<code> to
rem build\release-logs\<target>.exit when it ends. Wait for the .exit files; never
rem wait for the consoles. Targets: windows mac linux wasm, verify-windows verify-mac
rem verify-linux (the release tests, after the builders), upload-wasm upload-all
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
start "Patchy release - %~1" /min /belownormal cmd /c ""%~dp0release-worker.bat" %~1"
exit /b 0

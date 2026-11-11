@echo off
rem Runs one release target unattended: see release-all-automated.bat for the list
rem and the reasons behind each environment setting. Usage: release-worker.bat <target>
rem Output goes to build\release-logs\<target>.log and the exit code to
rem build\release-logs\<target>.exit as exit=<code>. The marker is written with the
rem redirect first (>file echo ...): cmd treats a digit directly before > as a file
rem handle, so echo %ERRORLEVEL%>file would leave an empty marker for exit code 0.
setlocal EnableExtensions
set "NO_PAUSE=1"
rem release-all-automated.bat lowers PATCHY_RELEASE_JOBS when two local builds share the box.
if not defined PATCHY_RELEASE_JOBS set "PATCHY_RELEASE_JOBS=20"
set "CMAKE_BUILD_PARALLEL_LEVEL=%PATCHY_RELEASE_JOBS%"
set "PSModulePath=%USERPROFILE%\Documents\WindowsPowerShell\Modules;%ProgramFiles%\WindowsPowerShell\Modules;%SystemRoot%\system32\WindowsPowerShell\v1.0\Modules"
cd /d "%~dp0..\.."
if not exist build\release-logs mkdir build\release-logs
set "LOG=build\release-logs\%~1.log"
set "MARKER=build\release-logs\%~1.exit"
if exist "%MARKER%" del "%MARKER%"
rem Every target runs with stdin from NUL: an ssh.exe that inherits this console as stdin can
rem stay alive after its remote command ends (docs/release-process-scripts.md). The scripts pass
rem ssh -n as well (check-ssh-stdin.py); this covers anything they start besides.
if /i "%~1"=="windows" ( call "%~dp0build-release.bat" <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="wasm" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\remote\release-wasm.ps1" <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="wasm-local" ( call "%~dp0build-wasm.bat" <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="mac" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\remote\release-mac.ps1" <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="linux" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\remote\release-linux.ps1" <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="upload-wasm" ( call "%~dp0upload-wasm-to-rtsoft.bat" nopause <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="upload-all" ( call "%~dp0upload-to-rtsoft.bat" <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="upload-desktop" ( call "%~dp0upload-to-rtsoft.bat" desktop <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="verify-windows" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-windows.ps1" <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="verify-mac" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-remote.ps1" -Target mac <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="verify-linux" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-remote.ps1" -Target linux <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="verify-windows-arm64" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0verify-remote.ps1" -Target windows-arm64 <nul > "%LOG%" 2>&1 & goto done )
rem Before the version bump (docs/release-process.md): the warning check and the rehearsal
rem run on the build hosts only, so they never meet this PC's build lock.
if /i "%~1"=="warncheck-windows" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0warncheck.ps1" -Target windows <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="warncheck-windows-arm64" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0warncheck.ps1" -Target windows-arm64 <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="warncheck-mac" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0warncheck.ps1" -Target mac <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="warncheck-linux" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0warncheck.ps1" -Target linux <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="warncheck-wasm" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0warncheck.ps1" -Target wasm <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="rehearse-windows" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0rehearse.ps1" -Target windows <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="rehearse-windows-arm64" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0rehearse.ps1" -Target windows-arm64 <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="rehearse-mac" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0rehearse.ps1" -Target mac <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="rehearse-linux" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0rehearse.ps1" -Target linux <nul > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="upload-github" ( call "%~dp0publish-github-release.bat" nopause <nul > "%LOG%" 2>&1 & goto done )
echo Unknown release target "%~1" ^(expected windows, mac, linux, wasm, wasm-local, verify-windows, verify-windows-arm64, verify-mac, verify-linux, warncheck-windows, warncheck-windows-arm64, warncheck-mac, warncheck-linux, warncheck-wasm, rehearse-windows, rehearse-windows-arm64, rehearse-mac, rehearse-linux, upload-wasm, upload-all, upload-desktop, or upload-github^). > "%LOG%"
>"%MARKER%" echo exit=2
exit /b 2

:done
set "CODE=%ERRORLEVEL%"
>"%MARKER%" echo exit=%CODE%
exit /b %CODE%

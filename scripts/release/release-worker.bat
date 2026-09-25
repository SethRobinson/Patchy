@echo off
rem Runs one release target unattended: see release-all-automated.bat for the list
rem and the reasons behind each environment setting. Usage: release-worker.bat <target>
rem Output goes to build\release-logs\<target>.log and the exit code to
rem build\release-logs\<target>.exit as exit=<code>. The marker is written with the
rem redirect first (>file echo ...): cmd treats a digit directly before > as a file
rem handle, so echo %ERRORLEVEL%>file would leave an empty marker for exit code 0.
setlocal EnableExtensions
set "NO_PAUSE=1"
set "CMAKE_BUILD_PARALLEL_LEVEL=20"
set "PSModulePath=%USERPROFILE%\Documents\WindowsPowerShell\Modules;%ProgramFiles%\WindowsPowerShell\Modules;%SystemRoot%\system32\WindowsPowerShell\v1.0\Modules"
cd /d "%~dp0..\.."
if not exist build\release-logs mkdir build\release-logs
set "LOG=build\release-logs\%~1.log"
set "MARKER=build\release-logs\%~1.exit"
if exist "%MARKER%" del "%MARKER%"
if /i "%~1"=="windows" ( call "%~dp0build-release.bat" > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="wasm" ( call "%~dp0build-wasm.bat" > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="mac" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\remote\release-mac.ps1" > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="linux" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\remote\release-linux.ps1" > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="upload-wasm" ( call "%~dp0upload-wasm-to-rtsoft.bat" nopause > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="upload-all" ( call "%~dp0upload-to-rtsoft.bat" > "%LOG%" 2>&1 & goto done )
if /i "%~1"=="upload-github" ( call "%~dp0publish-github-release.bat" nopause > "%LOG%" 2>&1 & goto done )
echo Unknown release target "%~1" ^(expected windows, mac, linux, wasm, upload-wasm, upload-all, or upload-github^). > "%LOG%"
>"%MARKER%" echo exit=2
exit /b 2

:done
set "CODE=%ERRORLEVEL%"
>"%MARKER%" echo exit=%CODE%
exit /b %CODE%

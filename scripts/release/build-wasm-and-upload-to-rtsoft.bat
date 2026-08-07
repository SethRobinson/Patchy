@echo off
setlocal EnableExtensions
rem Convenience wrapper: build + stage the wasm site, then upload it to
rem rtsoft.com/patchy. Calls its siblings by full path (%~dp0), never bare
rem name: NoDefaultCurrentDirectoryInExePath in non-interactive shells makes
rem a bare "call build-wasm.bat" die with "not recognized" unless the cwd
rem happens to be scripts\release (see docs/release-process.md). Stops with
rem an error instead of uploading when the build fails; a "nopause" first
rem argument (forwarded to the upload script) or NO_PAUSE=1 suppresses the
rem interactive pause for automated runs.

call "%~dp0build-wasm.bat"
if errorlevel 1 goto build_fail

call "%~dp0upload-wasm-to-rtsoft.bat" %1
exit /b %errorlevel%

:build_fail
echo The wasm build/staging failed; skipping the upload.
if /i not "%~1"=="nopause" if not defined NO_PAUSE pause
exit /b 1

@echo off
rem Uploads every platform's release artifacts (stable "latest" names), plus the
rem wasm site to rtsoft.com/patchy. Per-platform: upload-windows-to-rtsoft.bat /
rem upload-mac-to-rtsoft.bat / upload-linux-to-rtsoft.bat /
rem upload-wasm-to-rtsoft.bat (each also runs standalone).
rem cd to the repo root (this script lives in scripts\release) so the relative
rem build\package paths in the per-platform scripts resolve no matter the launch
rem cwd, and call the siblings by full path: cmd won't search the current directory
rem for them when NoDefaultCurrentDirectoryInExePath is set (e.g. a non-interactive
rem shell).
rem
rem Every step's exit code is checked and a failure is carried to the end. It used
rem to run all four unconditionally and exit 0 no matter what happened, so a failed
rem upload scrolled past and the release looked published (September 2026). The
rem later platforms still run after one fails, because publishing three of four is
rem better than stopping at the first, but the summary below names what did not
rem land and the script exits non-zero.
cd /d "%~dp0..\.."
set "FAILED="

call "%~dp0upload-windows-to-rtsoft.bat" nopause
if errorlevel 1 set "FAILED=%FAILED% Windows"
call "%~dp0upload-mac-to-rtsoft.bat" nopause
if errorlevel 1 set "FAILED=%FAILED% macOS"
call "%~dp0upload-linux-to-rtsoft.bat" nopause
if errorlevel 1 set "FAILED=%FAILED% Linux"
call "%~dp0upload-wasm-to-rtsoft.bat" nopause
if errorlevel 1 set "FAILED=%FAILED% wasm"

echo.
if defined FAILED (
  echo ==========================================================
  echo  RELEASE UPLOAD FAILED for:%FAILED%
  echo  Those downloads are stale or broken on rtsoft.com.
  echo  Fix the cause and re-run, and do not announce the release.
  echo ==========================================================
  pause
  exit /b 1
)
echo ==========================================================
echo  All release uploads verified on rtsoft.com.
echo ==========================================================
pause
exit /b 0

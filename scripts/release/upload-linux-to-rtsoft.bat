@echo off
rem Uploads the newest Linux Flatpak bundle (built by scripts\remote\release-linux.bat)
rem under the stable "latest" name LienzoLinux.flatpak that latest_version.json points at.
rem cd to the repo root (this script lives in scripts\release) so the relative paths
rem below resolve from any cwd.
cd /d "%~dp0..\.."
set "LINUX_BUNDLE="
for /f "delims=" %%F in ('dir /b /o:d build\package\Lienzo-*.flatpak 2^>nul') do set "LINUX_BUNDLE=build\package\%%F"
if not defined LINUX_BUNDLE (
  echo No Lienzo-*.flatpak found in build\package - run scripts\remote\release-linux.bat first.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
echo Uploading %LINUX_BUNDLE% as LienzoLinux.flatpak
copy /y "%LINUX_BUNDLE%" build\package\LienzoLinux.flatpak >nul
if errorlevel 1 (
  echo ERROR: could not stage build\package\LienzoLinux.flatpak from "%LINUX_BUNDLE%".
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
rem upload-one-file.bat fails loudly on a bad transfer and verifies the bytes that
rem landed; see its header for why a plain scp is not enough.
call "%~dp0upload-one-file.bat" build\package\LienzoLinux.flatpak files
if errorlevel 1 (
  echo.
  echo Linux upload FAILED - https://rtsoft.com/files/LienzoLinux.flatpak was not
  echo updated, or was left in a bad state. Do not announce this release.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
echo Linux upload OK: https://rtsoft.com/files/LienzoLinux.flatpak
if /i not "%~1"=="nopause" pause
exit /b 0

@echo off
rem Uploads the newest mac DMG (built by scripts\remote\release-mac.bat) under the
rem stable "latest" name LienzoMacOS.dmg that latest_version.json points at.
rem cd to the repo root (this script lives in scripts\release) so the relative paths
rem below resolve from any cwd.
cd /d "%~dp0..\.."
set "MAC_DMG="
for /f "delims=" %%F in ('dir /b /o:d build\package\Lienzo-*.dmg 2^>nul') do set "MAC_DMG=build\package\%%F"
if not defined MAC_DMG (
  echo No Lienzo-*.dmg found in build\package - run scripts\remote\release-mac.bat first.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
echo Uploading %MAC_DMG% as LienzoMacOS.dmg
copy /y "%MAC_DMG%" build\package\LienzoMacOS.dmg >nul
if errorlevel 1 (
  echo ERROR: could not stage build\package\LienzoMacOS.dmg from "%MAC_DMG%".
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
rem upload-one-file.bat fails loudly on a bad transfer and verifies the bytes that
rem landed; see its header for why a plain scp is not enough. Called by full path
rem because cmd will not search the current directory when
rem NoDefaultCurrentDirectoryInExePath is set.
call "%~dp0upload-one-file.bat" build\package\LienzoMacOS.dmg files
if errorlevel 1 (
  echo.
  echo macOS upload FAILED - https://rtsoft.com/files/LienzoMacOS.dmg was not updated
  echo or was left in a bad state. Do not announce this release.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
echo macOS upload OK: https://rtsoft.com/files/LienzoMacOS.dmg
if /i not "%~1"=="nopause" pause
exit /b 0

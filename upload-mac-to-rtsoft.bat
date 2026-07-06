@echo off
rem Uploads the newest mac DMG (built by scripts\remote\release-mac.bat) under the
rem stable "latest" name PatchyMacOS.dmg that latest_version.json points at.
set "MAC_DMG="
for /f "delims=" %%F in ('dir /b /o:d build\package\Patchy-*.dmg 2^>nul') do set "MAC_DMG=build\package\%%F"
if not defined MAC_DMG (
  echo No Patchy-*.dmg found in build\package - run scripts\remote\release-mac.bat first.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
echo Uploading %MAC_DMG% as PatchyMacOS.dmg
copy /y "%MAC_DMG%" build\package\PatchyMacOS.dmg >nul
call %RT_PROJECTS%\UploadFileToRTsoftSSH.bat build\package\PatchyMacOS.dmg files
if /i not "%~1"=="nopause" pause

@echo off
rem Uploads the newest Linux Flatpak bundle (built by scripts\remote\release-linux.bat)
rem under the stable "latest" name PatchyLinux.flatpak that latest_version.json points at.
set "LINUX_BUNDLE="
for /f "delims=" %%F in ('dir /b /o:d build\package\Patchy-*.flatpak 2^>nul') do set "LINUX_BUNDLE=build\package\%%F"
if not defined LINUX_BUNDLE (
  echo No Patchy-*.flatpak found in build\package - run scripts\remote\release-linux.bat first.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
echo Uploading %LINUX_BUNDLE% as PatchyLinux.flatpak
copy /y "%LINUX_BUNDLE%" build\package\PatchyLinux.flatpak >nul
call %RT_PROJECTS%\UploadFileToRTsoftSSH.bat build\package\PatchyLinux.flatpak files
if /i not "%~1"=="nopause" pause

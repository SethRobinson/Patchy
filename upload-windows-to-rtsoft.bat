@echo off
rem Uploads the Windows release artifacts (stable names, built by build-release.bat).
rem Missing files abort loudly: build-release.bat deletes its outputs up front, so a
rem failed build leaves nothing here and this refuses rather than uploading stale bits.
if not exist build\package\PatchyWindowsInstaller.exe (
  echo PatchyWindowsInstaller.exe is missing from build\package - run build-release.bat first.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
if not exist build\package\PatchyWindowsNoInstaller.zip (
  echo PatchyWindowsNoInstaller.zip is missing from build\package - run build-release.bat first.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
call %RT_PROJECTS%\UploadFileToRTsoftSSH.bat build\package\PatchyWindowsInstaller.exe files
call %RT_PROJECTS%\UploadFileToRTsoftSSH.bat build\package\PatchyWindowsNoInstaller.zip files
if /i not "%~1"=="nopause" pause

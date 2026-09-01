@echo off
rem Uploads the Windows release artifacts (stable names, built by build-release.bat).
rem Missing files abort loudly: build-release.bat deletes its outputs up front, so a
rem failed build leaves nothing here and this refuses rather than uploading stale bits.
rem cd to the repo root (this script lives in scripts\release) so the relative paths
rem below resolve from any cwd.
cd /d "%~dp0..\.."
if not exist build\package\PatchyWindowsInstaller.exe (
  echo PatchyWindowsInstaller.exe is missing from build\package - run scripts\release\build-release.bat first.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
if not exist build\package\PatchyWindowsNoInstaller.zip (
  echo PatchyWindowsNoInstaller.zip is missing from build\package - run scripts\release\build-release.bat first.
  if /i not "%~1"=="nopause" pause
  exit /b 1
)
rem upload-one-file.bat fails loudly on a bad transfer and verifies the bytes that
rem landed; see its header for why a plain scp is not enough. The installer goes
rem first: if the second upload dies, the site is left advertising a matched pair
rem only when both succeeded, and the failure message says so.
call "%~dp0upload-one-file.bat" build\package\PatchyWindowsInstaller.exe files
if errorlevel 1 goto fail
call "%~dp0upload-one-file.bat" build\package\PatchyWindowsNoInstaller.zip files
if errorlevel 1 goto fail
echo Windows upload OK: https://rtsoft.com/files/PatchyWindowsInstaller.exe
echo                    https://rtsoft.com/files/PatchyWindowsNoInstaller.zip
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo.
echo Windows upload FAILED - the two Windows downloads may now disagree with each
echo other or with this build. Re-run this script; do not announce the release.
if /i not "%~1"=="nopause" pause
exit /b 1

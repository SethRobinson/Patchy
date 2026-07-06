@echo off
rem Uploads the Windows release artifacts (stable names, built by build-release.bat).
call %RT_PROJECTS%\UploadFileToRTsoftSSH.bat build\package\PatchyWindowsInstaller.exe files
call %RT_PROJECTS%\UploadFileToRTsoftSSH.bat build\package\PatchyWindowsNoInstaller.zip files
if /i not "%~1"=="nopause" pause

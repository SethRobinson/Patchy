@echo off
rem Double-clickable wrapper: runs the whole remote macOS release (sync working tree,
rem build, macdeployqt, codesign, dmg, notarize, staple, copy dmg back) with live
rem output in this window. See scripts\remote\release-mac.ps1 for the real logic.
cd /d "%~dp0..\.."
powershell -ExecutionPolicy Bypass -File "scripts\remote\release-mac.ps1"
echo.
pause

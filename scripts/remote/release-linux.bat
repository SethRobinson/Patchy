@echo off
rem Double-clickable wrapper: runs the whole remote Linux release (sync working tree,
rem build, flatpak-builder, bundle, copy .flatpak back) with live output in this
rem window. See scripts\remote\release-linux.ps1 for the real logic.
cd /d "%~dp0..\.."
powershell -ExecutionPolicy Bypass -File "scripts\remote\release-linux.ps1"
echo.
pause

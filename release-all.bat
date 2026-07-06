@echo off
rem Builds all three platform releases at once, each in its own console window so the
rem progress of every one is visible:
rem   - Windows: build-release.bat (local build, zip + installer, code signing)
rem   - macOS:   scripts\remote\release-mac.bat (build on studiomac, sign, notarize, dmg)
rem   - Linux:   scripts\remote\release-linux.bat (build on glados, flatpak bundle)
rem Every builder deletes its previous artifacts up front, so a failed window leaves
rem nothing stale behind. When all three windows are done, run upload-to-rtsoft.bat.
cd /d "%~dp0"
start "Patchy release - Windows" cmd /c "build-release.bat"
start "Patchy release - macOS" cmd /c "scripts\remote\release-mac.bat"
start "Patchy release - Linux" cmd /c "scripts\remote\release-linux.bat"
echo Three release windows launched. When they all finish, run upload-to-rtsoft.bat.

@echo off
rem Builds all three platform releases at once, each in its own console window so the
rem progress of every one is visible:
rem   - Windows: build-release.bat (local build, zip + installer, code signing)
rem   - macOS:   scripts\remote\release-mac.bat (build on studiomac, sign, notarize, dmg)
rem   - Linux:   scripts\remote\release-linux.bat (build on glados, flatpak bundle)
rem Every builder deletes its previous artifacts up front, so a failed window leaves
rem nothing stale behind. When all three windows are done, run upload-to-rtsoft.bat.
rem Launch the builders by full path: cmd won't search the current directory for a
rem bare command name when NoDefaultCurrentDirectoryInExePath is set (non-interactive
rem shells set it), and the console closes too fast to read the error. See
rem docs/release-process.md.
cd /d "%~dp0"
start "Patchy release - Windows" cmd /c "%~dp0build-release.bat"
start "Patchy release - macOS" cmd /c "%~dp0scripts\remote\release-mac.bat"
start "Patchy release - Linux" cmd /c "%~dp0scripts\remote\release-linux.bat"
echo Three release windows launched. When they all finish, run upload-to-rtsoft.bat.

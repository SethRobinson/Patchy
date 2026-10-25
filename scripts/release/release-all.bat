@echo off
rem Builds all four releases at once, each in its own console window so the
rem progress of every one is visible:
rem   - Windows: build-release.bat (local build, zip + installer, code signing)
rem   - macOS:   scripts\remote\release-mac.bat (build on the mac build host, sign, notarize, dmg)
rem   - Linux:   scripts\remote\release-linux.bat (build on the linux build host, flatpak bundle)
rem   - Web:     build-wasm.bat (local Qt-for-WebAssembly build, staged for rtsoft.com/patchy)
rem Every builder deletes its previous artifacts up front, so a failed window leaves
rem nothing stale behind. After all four builders and the required tests pass, commit
rem the build tree with latest_version.json still announcing the old release.
rem Upload the mirror, push the build commit, then publish the GitHub release.
rem Verify its public downloads before committing/pushing the new update manifest.
rem Launch the builders by full path: cmd won't search the current directory for a
rem bare command name when NoDefaultCurrentDirectoryInExePath is set (non-interactive
rem shells set it), and the console closes too fast to read the error. See
rem docs/release-process.md.
rem This script lives in scripts\release, so %~dp0..\remote reaches scripts\remote.
cd /d "%~dp0..\.."
rem Preflight (same as release-all-automated.bat): the Linux metainfo must describe this
rem version, screenshot pins included, before any builder starts.
python "%~dp0check-metainfo.py"
if not "%ERRORLEVEL%"=="0" goto metainfo_failed
start "Patchy release - Windows" cmd /c "%~dp0build-release.bat"
start "Patchy release - macOS" cmd /c "%~dp0..\remote\release-mac.bat"
start "Patchy release - Linux" cmd /c "%~dp0..\remote\release-linux.bat"
start "Patchy release - Web (wasm)" cmd /c "%~dp0build-wasm.bat"
echo Four release windows launched. When they all finish, run scripts\release\upload-to-rtsoft.bat.
exit /b 0

:metainfo_failed
echo.
echo RELEASE PREFLIGHT FAILED: packaging\linux\com.rtsoft.patchy.metainfo.xml does not describe
echo the version in CMakeLists.txt ^(see the errors above^). No builder was launched. Fix it with
echo   python scripts\release\check-metainfo.py --fix
echo and rerun. Python 3 must be on PATH.
if not defined NO_PAUSE pause
exit /b 1

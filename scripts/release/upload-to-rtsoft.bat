@echo off
rem Release uploads to rtsoft.com: the signed Flatpak repository (rtsoft.com/flatpak,
rem upload-flatpak-repo-to-rtsoft.bat) and the wasm site (rtsoft.com/patchy,
rem upload-wasm-to-rtsoft.bat); each also runs standalone. The desktop downloads are on
rem GitHub Releases only (publish-github-release.bat): rtsoft.com stopped mirroring them
rem with 1.25. With the argument "desktop" the wasm site is left alone: a release that
rem ships no new web build must not upload the previous one again
rem (release-all-automated.bat upload-desktop).
rem cd to the repo root (this script lives in scripts\release) so the relative
rem build\package paths in the per-platform scripts resolve no matter the launch
rem cwd, and call the siblings by full path: cmd won't search the current directory
rem for them when NoDefaultCurrentDirectoryInExePath is set (e.g. a non-interactive
rem shell).
rem
rem Every step's exit code is checked and a failure is carried to the end, so a failed
rem upload never scrolls past looking published (September 2026): the summary below
rem names what did not land and the script exits non-zero.
cd /d "%~dp0..\.."
set "FAILED="

call "%~dp0upload-flatpak-repo-to-rtsoft.bat" nopause
if errorlevel 1 set "FAILED=%FAILED% Flatpak-repository"
if /i "%~1"=="desktop" goto uploads_done
call "%~dp0upload-wasm-to-rtsoft.bat" nopause
if errorlevel 1 set "FAILED=%FAILED% wasm"
:uploads_done

echo.
if defined FAILED (
  echo ==========================================================
  echo  RELEASE UPLOAD FAILED for:%FAILED%
  echo  Fix the cause and re-run, and do not announce the release.
  echo ==========================================================
  if not defined NO_PAUSE pause
  exit /b 1
)
echo ==========================================================
echo  rtsoft.com uploads verified ^(Flatpak repository, web site^).
echo  The downloads are on GitHub: after the release commit is
echo  pushed, run scripts\release\publish-github-release.bat.
echo ==========================================================
if not defined NO_PAUSE pause
exit /b 0

@echo off
rem Publishes the release in build\package to GitHub Releases (tag v<version>, the four
rem desktop artifacts, SHA256SUMS.txt) as a verified draft-then-public release. GitHub
rem is the canonical download location; upload-to-rtsoft.bat keeps the rtsoft.com
rem mirror. The release commit must be committed and pushed to origin/main first.
rem Extra arguments are passed to publish-github-release.ps1 (-Version, -Target,
rem -AssetDir for a backfill). "nopause" as the first argument, or NO_PAUSE, skips the
rem final pause. cd to the repo root (this script lives in scripts\release).
cd /d "%~dp0..\.."
rem Windows PowerShell 5.1 module path, so a launch from pwsh 7 does not hand the 5.1
rem process pwsh's own modules (see docs/release-process.md, agent/non-interactive runs).
set "PSModulePath=%USERPROFILE%\Documents\WindowsPowerShell\Modules;%ProgramFiles%\WindowsPowerShell\Modules;%SystemRoot%\system32\WindowsPowerShell\v1.0\Modules"
set "PGR_ARGS=%*"
if /i "%~1"=="nopause" (
  set "PGR_NOPAUSE=1"
  for /f "tokens=1,* delims= " %%A in ("%*") do set "PGR_ARGS=%%B"
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0publish-github-release.ps1" %PGR_ARGS%
set "PGR_CODE=%ERRORLEVEL%"
if not "%PGR_CODE%"=="0" (
  echo.
  echo GitHub release publish FAILED ^(exit %PGR_CODE%^). See the messages above; a
  echo draft, if one was created, is invisible to visitors and is reused on re-run.
)
if not defined NO_PAUSE if not defined PGR_NOPAUSE pause
exit /b %PGR_CODE%

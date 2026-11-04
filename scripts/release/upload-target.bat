@echo off
rem Sets UPLOAD_SSH, the ssh target (user@host) of the download server that the release upload
rem scripts write to. The account is developer-specific, so it lives in the "upload" entry of
rem scripts\remote\hosts.local.json (gitignored; see hosts.example.json; a worktree reads the main
rem checkout's copy), never in the public repository. PATCHY_UPLOAD_SSH overrides it. Callers:
rem   call "%~dp0upload-target.bat"
rem   if errorlevel 1 (their own failure path)
if defined PATCHY_UPLOAD_SSH (
  set "UPLOAD_SSH=%PATCHY_UPLOAD_SSH%"
  exit /b 0
)
set "UPLOAD_SSH="
for /f "usebackq delims=" %%U in (`powershell -NoProfile -ExecutionPolicy Bypass -Command ". '%~dp0..\remote\remote-hosts.ps1'; (Get-PatchyRemoteHost upload).ssh"`) do set "UPLOAD_SSH=%%U"
if not defined UPLOAD_SSH (
  echo ERROR: no upload target. Add an "upload" entry with an "ssh" value to
  echo scripts\remote\hosts.local.json ^(see scripts\remote\hosts.example.json^), or set PATCHY_UPLOAD_SSH.
  exit /b 1
)
exit /b 0

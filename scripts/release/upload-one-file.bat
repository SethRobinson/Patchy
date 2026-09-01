@echo off
rem Uploads ONE release artifact to rtsoft.com and proves it arrived intact.
rem
rem   scripts\release\upload-one-file.bat <local-file> <remote-subdir>
rem
rem Never pauses and never prompts: the per-platform caller owns that. Returns 0
rem only when the bytes on the server match the bytes on disk.
rem
rem This replaces the %RT_PROJECTS%\UploadFileToRTsoftSSH.bat call the release
rem upload scripts used to make. That helper is a bare scp whose exit code nobody
rem checked, so a refused connection, a full disk, or a half-written transfer
rem looked exactly like a successful publish (found September 2026 while shipping
rem a macOS-only release: the upload "succeeded" with exit 0, and the only real
rem proof was a curl run by hand afterwards). Keep every release upload going
rem through here rather than reintroducing a fire-and-forget copy.
rem
rem Verification is a SHA-256 comparison rather than a size check: scp usually
rem exits non-zero on a partial write, but the case worth catching is the one that
rem exits 0 with wrong bytes on the far end. The server hashes what actually
rem landed and the two must agree.
rem
rem Both captures use `usebackq` with a backquoted command so the embedded double
rem quotes survive, and neither command contains a pipe: an unescaped `|` inside
rem for /f ('...') is parsed by cmd itself, which is the same class of quoting bug
rem as the 2^>nul escapes elsewhere in these scripts.
rem
rem cd to the repo root (this script lives in scripts\release) so a relative local
rem path resolves from any cwd.
setlocal
cd /d "%~dp0..\.."

set "LOCAL_FILE=%~1"
set "REMOTE_DIR=%~2"
if "%LOCAL_FILE%"=="" (
  echo upload-one-file.bat: no local file given.
  exit /b 1
)
if "%REMOTE_DIR%"=="" (
  echo upload-one-file.bat: no remote directory given.
  exit /b 1
)
if not exist "%LOCAL_FILE%" (
  echo ERROR: "%LOCAL_FILE%" does not exist, so there is nothing to upload.
  exit /b 1
)
for %%F in ("%LOCAL_FILE%") do set "REMOTE_NAME=%%~nxF"

rem Get-FileHash rather than certutil: one clean lowercase hex line to compare,
rem with no header or "completed successfully" trailer to strip.
set "LOCAL_HASH="
for /f "usebackq delims=" %%H in (`powershell -NoProfile -Command "(Get-FileHash -LiteralPath '%LOCAL_FILE%' -Algorithm SHA256).Hash.ToLower()"`) do set "LOCAL_HASH=%%H"
if not defined LOCAL_HASH (
  echo ERROR: could not hash "%LOCAL_FILE%" locally; refusing to upload bytes we cannot verify.
  exit /b 1
)

echo Uploading %LOCAL_FILE% to rtsoft.com:www/%REMOTE_DIR%/%REMOTE_NAME% ...
scp "%LOCAL_FILE%" rtsoft@rtsoft.com:www/%REMOTE_DIR%/
if errorlevel 1 (
  echo.
  echo ERROR: scp failed for "%LOCAL_FILE%".
  echo Nothing was verified, so assume the copy on the server is stale or partial.
  exit /b 1
)

rem sha256sum prints "<hash>  <path>"; take the first token rather than piping to
rem cut. A missing file writes to stderr and prints nothing, so REMOTE_HASH stays
rem undefined and the check below fires.
echo Verifying the uploaded copy on the server ...
set "REMOTE_HASH="
for /f "usebackq tokens=1" %%H in (`ssh rtsoft@rtsoft.com "sha256sum www/%REMOTE_DIR%/%REMOTE_NAME%"`) do set "REMOTE_HASH=%%H"
if not defined REMOTE_HASH (
  echo.
  echo ERROR: could not read back a SHA-256 for www/%REMOTE_DIR%/%REMOTE_NAME%.
  echo The transfer reported success but the file could not be hashed on the
  echo server, so it may be missing. Check by hand with:
  echo   ssh rtsoft@rtsoft.com "ls -l www/%REMOTE_DIR%/%REMOTE_NAME%"
  exit /b 1
)
if /i not "%LOCAL_HASH%"=="%REMOTE_HASH%" (
  echo.
  echo ERROR: the uploaded file does not match the local one.
  echo   local:  %LOCAL_HASH%
  echo   server: %REMOTE_HASH%
  echo The published file is WRONG. Re-run this upload; do not announce the release.
  exit /b 1
)

echo Verified %REMOTE_NAME% ^(sha256 %LOCAL_HASH%^)
exit /b 0

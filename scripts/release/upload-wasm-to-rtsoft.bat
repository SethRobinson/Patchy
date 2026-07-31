@echo off
rem Uploads the staged wasm site (built by build-wasm.bat) to rtsoft.com/patchy.
rem Missing files abort loudly: build-wasm.bat deletes its staging directory up
rem front, so a failed build leaves nothing here and this refuses rather than
rem uploading stale bits. Talks to ssh/scp directly instead of going through
rem UploadFileToRTsoftSSH.bat: this is a multi-file directory install that needs
rem the remote directory created first and a hard stop on a failed transfer.
rem The single scp lists the html files last, so a visitor who loads the page
rem mid-upload never gets new html pointing at assets that are not there yet.
rem cd to the repo root (this script lives in scripts\release) so the relative
rem paths below resolve from any cwd.
cd /d "%~dp0..\.."
set "SITE_DIR=build\package\wasm-site"
for %%F in (patchy.data patchy.wasm patchy.js qtloader.js patchy.data.br patchy.data.gz patchy.wasm.br patchy.wasm.gz patchy.js.br patchy.js.gz qtloader.js.br qtloader.js.gz patchy-logo.png favicon.ico NOTICE-THIRD-PARTY.md libheif-COPYING.txt .htaccess patchy.html index.html) do (
  if not exist "%SITE_DIR%\%%F" (
    echo %%F is missing from %SITE_DIR% - run scripts\release\build-wasm.bat first.
    if /i not "%~1"=="nopause" pause
    exit /b 1
  )
)
echo Ensuring www/patchy exists on rtsoft.com...
ssh rtsoft@rtsoft.com "mkdir -p www/patchy"
if errorlevel 1 goto fail
echo Uploading the wasm site to rtsoft.com/patchy...
scp "%SITE_DIR%\patchy.data" "%SITE_DIR%\patchy.wasm" "%SITE_DIR%\patchy.js" "%SITE_DIR%\qtloader.js" "%SITE_DIR%\patchy.data.br" "%SITE_DIR%\patchy.data.gz" "%SITE_DIR%\patchy.wasm.br" "%SITE_DIR%\patchy.wasm.gz" "%SITE_DIR%\patchy.js.br" "%SITE_DIR%\patchy.js.gz" "%SITE_DIR%\qtloader.js.br" "%SITE_DIR%\qtloader.js.gz" "%SITE_DIR%\patchy-logo.png" "%SITE_DIR%\favicon.ico" "%SITE_DIR%\NOTICE-THIRD-PARTY.md" "%SITE_DIR%\libheif-COPYING.txt" "%SITE_DIR%\.htaccess" "%SITE_DIR%\patchy.html" "%SITE_DIR%\index.html" rtsoft@rtsoft.com:www/patchy/
if errorlevel 1 goto fail
rem The multithreaded app dies without cross-origin isolation, and .htaccess
rem directives are IfModule-guarded (a host missing mod_headers serves the
rem site with the headers silently dropped), so verify the live headers.
echo Verifying cross-origin isolation headers on the live site...
curl -sI https://www.rtsoft.com/patchy/ | findstr /i "Cross-Origin-Opener-Policy" >nul || goto headers_fail
curl -sI https://www.rtsoft.com/patchy/ | findstr /i "Cross-Origin-Embedder-Policy" >nul || goto headers_fail
rem Compressed serving is a bandwidth optimization, not a correctness gate:
rem without mod_rewrite/mod_mime the identity files still serve, so a missing
rem Content-Encoding warns instead of failing the upload.
echo Verifying compressed serving on the live site...
curl -sI -H "Accept-Encoding: br, gzip" https://www.rtsoft.com/patchy/patchy.wasm | findstr /i "content-encoding" >nul
if errorlevel 1 (
  echo WARNING: the live site is serving patchy.wasm without compression.
  echo The precompressed .br/.gz files were uploaded, but the server did not
  echo answer an encoded response; check mod_rewrite and mod_mime, then
  echo re-check with: curl -sI -H "Accept-Encoding: br" https://www.rtsoft.com/patchy/patchy.wasm
)
echo Wasm site uploaded: https://www.rtsoft.com/patchy/
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo Wasm site upload failed.
if /i not "%~1"=="nopause" pause
exit /b 1

:headers_fail
echo The live site is missing the Cross-Origin-Opener-Policy or
echo Cross-Origin-Embedder-Policy response header, so the multithreaded app
echo cannot start (visitors see the isolation error page). Enable mod_headers
echo on the server or add the headers to the server config, then re-check
echo with: curl -sI https://www.rtsoft.com/patchy/
if /i not "%~1"=="nopause" pause
exit /b 1

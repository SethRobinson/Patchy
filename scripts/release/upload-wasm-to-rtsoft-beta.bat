@echo off
rem Uploads the staged wasm site (built by build-wasm.bat) to a STAGING copy at
rem rtsoft.com/patchy-beta for Safari/iOS device testing, leaving the production
rem /patchy site untouched. Mirror of upload-wasm-to-rtsoft.bat with two
rem differences: the target directory, and the memory-diagnostics harness
rem (stress-harness.html + memsoak.js) is included; it must never ship to
rem production. See docs/performance.md (headless wasm stress harness).
cd /d "%~dp0..\.."
set "SITE_DIR=build\package\wasm-site"
for %%F in (patchy.data patchy.wasm patchy.js qtloader.js patchy.data.br patchy.data.gz patchy.wasm.br patchy.wasm.gz patchy.js.br patchy.js.gz qtloader.js.br qtloader.js.gz patchy-logo.png favicon.ico NOTICE-THIRD-PARTY.md libheif-COPYING.txt .htaccess patchy.html index.html stress-harness.html memsoak.js) do (
  if not exist "%SITE_DIR%\%%F" (
    echo %%F is missing from %SITE_DIR% - run scripts\release\build-wasm.bat first.
    if /i not "%~1"=="nopause" pause
    exit /b 1
  )
)
echo Ensuring www/patchy-beta exists on rtsoft.com...
ssh rtsoft@rtsoft.com "mkdir -p www/patchy-beta"
if errorlevel 1 goto fail
echo Uploading the wasm site to rtsoft.com/patchy-beta...
scp "%SITE_DIR%\patchy.data" "%SITE_DIR%\patchy.wasm" "%SITE_DIR%\patchy.js" "%SITE_DIR%\qtloader.js" "%SITE_DIR%\patchy.data.br" "%SITE_DIR%\patchy.data.gz" "%SITE_DIR%\patchy.wasm.br" "%SITE_DIR%\patchy.wasm.gz" "%SITE_DIR%\patchy.js.br" "%SITE_DIR%\patchy.js.gz" "%SITE_DIR%\qtloader.js.br" "%SITE_DIR%\qtloader.js.gz" "%SITE_DIR%\patchy-logo.png" "%SITE_DIR%\favicon.ico" "%SITE_DIR%\NOTICE-THIRD-PARTY.md" "%SITE_DIR%\libheif-COPYING.txt" "%SITE_DIR%\.htaccess" "%SITE_DIR%\memsoak.js" "%SITE_DIR%\stress-harness.html" "%SITE_DIR%\patchy.html" "%SITE_DIR%\index.html" rtsoft@rtsoft.com:www/patchy-beta/
if errorlevel 1 goto fail
echo Verifying cross-origin isolation headers on the beta site...
curl -sI https://www.rtsoft.com/patchy-beta/ | findstr /i "Cross-Origin-Opener-Policy" >nul || goto headers_fail
curl -sI https://www.rtsoft.com/patchy-beta/ | findstr /i "Cross-Origin-Embedder-Policy" >nul || goto headers_fail
echo Wasm beta site uploaded: https://www.rtsoft.com/patchy-beta/
echo Harness: https://www.rtsoft.com/patchy-beta/stress-harness.html
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
echo Wasm beta site upload failed.
if /i not "%~1"=="nopause" pause
exit /b 1

:headers_fail
echo The beta site is missing the Cross-Origin-Opener-Policy or
echo Cross-Origin-Embedder-Policy response header, so the multithreaded app
echo cannot start there. Enable mod_headers or add the headers to the server
echo config, then re-check with: curl -sI https://www.rtsoft.com/patchy-beta/
if /i not "%~1"=="nopause" pause
exit /b 1

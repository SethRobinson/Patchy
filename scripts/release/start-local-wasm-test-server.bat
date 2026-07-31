@echo off
setlocal EnableExtensions
rem Serves the raw wasm-release build directory (build\wasm-release) on
rem http://localhost:8973/patchy.html via scripts\wasm\serve-app.ps1 - the
rem development loop, straight from the last `cmake --build --preset
rem wasm-release`, with Qt's generated shell page. An optional first argument
rem overrides the port. (To test the staged, branded site exactly as
rem upload-wasm-to-rtsoft.bat would publish it, use
rem scripts\release\start-local-wasm-server.bat instead.)
rem
rem Run it repeatedly without cleaning up first: a previous local wasm server
rem still holding the port is stopped before the fresh one starts, and the
rem fresh one opens the page in the default browser (serve.mjs --open fires
rem the moment the socket is accepting, so the browser never races ahead of
rem the server).
rem cd to the repo root (this script lives in scripts\release) so the relative
rem paths below resolve from any cwd.
cd /d "%~dp0..\.."
if not exist build\wasm-release\patchy.html (
  echo build\wasm-release\patchy.html is missing - build the wasm-release preset first ^(see docs\wasm.md^).
  goto fail
)
set "PORT=%~1"
if not defined PORT set "PORT=8973"

rem Free the port if an earlier local server (this script, serve-app.ps1, or
rem start-local-wasm-server.bat) still holds it; node would otherwise die with
rem EADDRINUSE. The helper only ever stops a node process running serve.mjs.
set "PORT_STATE="
for /f "usebackq delims=" %%S in (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\wasm\free-server-port.ps1 -Port %PORT%`) do set "PORT_STATE=%%S"
if "%PORT_STATE:~0,5%"=="busy:" (
  echo Port %PORT% is held by %PORT_STATE:~5%, which is not a local wasm server.
  echo Close that program or pass a different port: scripts\release\start-local-wasm-test-server.bat 8974
  goto fail
)
if "%PORT_STATE:~0,7%"=="failed:" (
  echo Could not stop the process holding port %PORT% ^(%PORT_STATE:~7%^).
  goto fail
)
if "%PORT_STATE:~0,8%"=="stopped:" echo Stopped the previous local server on port %PORT% ^(%PORT_STATE:~8%^).

echo Starting the wasm dev server on http://localhost:%PORT%/patchy.html ^(opens in the default browser; Ctrl+C to stop^)...
pwsh -NoProfile -File scripts\wasm\serve-app.ps1 %PORT% --open
endlocal
exit /b 0

:fail
rem NO_PAUSE keeps automated runs from blocking here; see docs/release-process.md.
if not defined NO_PAUSE pause
endlocal
exit /b 1

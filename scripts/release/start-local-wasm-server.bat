@echo off
setlocal EnableExtensions
rem Serves the staged wasm site (build\package\wasm-site, made by build-wasm.bat)
rem on http://localhost:8973/ so the exact payload upload-wasm-to-rtsoft.bat would
rem publish can be tested in a browser first. An optional first argument overrides
rem the port. Uses the emsdk-bundled node with scripts\wasm\serve.mjs, which sends
rem the correct .wasm/.data MIME types. (To serve the raw build directory during
rem development, use scripts\wasm\serve-app.ps1 instead.)
rem
rem Run it repeatedly without cleaning up first: a previous instance of this
rem server still holding the port is stopped, then the fresh one starts and
rem opens the site in the default browser (serve.mjs --open fires the moment the
rem socket is accepting, so the browser never races ahead of the server).
rem cd to the repo root (this script lives in scripts\release) so the relative
rem paths below resolve from any cwd.
cd /d "%~dp0..\.."
if not exist build\package\wasm-site\index.html (
  echo build\package\wasm-site is missing - run scripts\release\build-wasm.bat first.
  goto fail
)
set "NODE_EXE="
for /d %%D in (.deps\emsdk\node\*) do if exist "%%D\bin\node.exe" set "NODE_EXE=%%D\bin\node.exe"
if not defined NODE_EXE (
  echo The emsdk-bundled node was not found. Run: pwsh -File scripts\wasm\setup-emsdk.ps1
  goto fail
)
set "PORT=%~1"
if not defined PORT set "PORT=8973"

rem Free the port if an earlier run of this script (or serve-app.ps1) is still
rem listening on it; node would otherwise die with EADDRINUSE. The helper only
rem ever stops a node process and reports anything else, and it lives in its own
rem .ps1 because cmd cannot parse literal parentheses inside a for /f backquote
rem command (see the comments in free-server-port.ps1).
set "PORT_STATE="
for /f "usebackq delims=" %%S in (`powershell -NoProfile -ExecutionPolicy Bypass -File scripts\wasm\free-server-port.ps1 -Port %PORT%`) do set "PORT_STATE=%%S"
if "%PORT_STATE:~0,5%"=="busy:" (
  echo Port %PORT% is held by %PORT_STATE:~5%, which is not a local wasm server.
  echo Close that program or pass a different port: scripts\release\start-local-wasm-server.bat 8974
  goto fail
)
if "%PORT_STATE:~0,7%"=="failed:" (
  echo Could not stop the process holding port %PORT% ^(%PORT_STATE:~7%^).
  goto fail
)
if "%PORT_STATE:~0,8%"=="stopped:" echo Stopped the previous local server on port %PORT% ^(%PORT_STATE:~8%^).

rem serve.mjs prints its own "serving ... at ..." line once the socket is
rem accepting, so this one only announces the start and how to stop it.
echo Starting the local wasm site server on port %PORT% ^(Ctrl+C to stop^)...
"%NODE_EXE%" scripts\wasm\serve.mjs build\package\wasm-site %PORT% --open
endlocal
exit /b 0

:fail
rem NO_PAUSE keeps automated runs from blocking here; see docs/release-process.md.
if not defined NO_PAUSE pause
endlocal
exit /b 1

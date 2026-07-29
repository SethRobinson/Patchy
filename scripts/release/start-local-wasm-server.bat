@echo off
rem Serves the staged wasm site (build\package\wasm-site, made by build-wasm.bat)
rem on http://localhost:8973/ so the exact payload upload-wasm-to-rtsoft.bat would
rem publish can be tested in a browser first. An optional first argument overrides
rem the port. Uses the emsdk-bundled node with scripts\wasm\serve.mjs, which sends
rem the correct .wasm/.data MIME types. (To serve the raw build directory during
rem development, use scripts\wasm\serve-app.ps1 instead.)
rem cd to the repo root (this script lives in scripts\release) so the relative
rem paths below resolve from any cwd.
cd /d "%~dp0..\.."
if not exist build\package\wasm-site\index.html (
  echo build\package\wasm-site is missing - run scripts\release\build-wasm.bat first.
  pause
  exit /b 1
)
set "NODE_EXE="
for /d %%D in (.deps\emsdk\node\*) do if exist "%%D\bin\node.exe" set "NODE_EXE=%%D\bin\node.exe"
if not defined NODE_EXE (
  echo The emsdk-bundled node was not found. Run: pwsh -File scripts\wasm\setup-emsdk.ps1
  pause
  exit /b 1
)
set "PORT=%~1"
if not defined PORT set "PORT=8973"
echo Serving build\package\wasm-site at http://localhost:%PORT%/ ^(Ctrl+C to stop^)
"%NODE_EXE%" scripts\wasm\serve.mjs build\package\wasm-site %PORT%

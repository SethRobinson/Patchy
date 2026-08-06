@echo off
setlocal EnableExtensions
rem Builds the wasm-release preset (the Qt for WebAssembly app; see docs/wasm.md)
rem and stages the deployable site files into build\package\wasm-site for
rem upload-wasm-to-rtsoft.bat. The staging directory is deleted up front, so a
rem failed build can never leave stale files behind for the upload script.
rem The served page comes from packaging\web\patchy.html.in (Patchy-branded
rem loading screen with a download progress bar), not Qt's generated shell:
rem this script substitutes the app version and a per-build cache tag into it,
rem so every deploy busts the browser cache via fresh ?v= query strings.
rem This script lives in scripts\release, two levels below the repo root.
pushd "%~dp0..\.." || exit /b 1
set "REPO=%CD%"

set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"

set "BUILD_DIR=%REPO%\build\wasm-release"
set "SITE_DIR=%REPO%\build\package\wasm-site"

if not exist "%REPO%\.deps\emsdk\emsdk_env.bat" (
  echo The Emscripten SDK was not found. Run: pwsh -File scripts\wasm\setup-emsdk.ps1
  goto fail
)
if not exist "%REPO%\.deps\Qt\6.10.3\wasm_multithread\lib\cmake\Qt6\qt.toolchain.cmake" (
  echo Qt for WebAssembly was not found. Run: pwsh -File scripts\wasm\setup-qt-wasm.ps1
  goto fail
)
if not exist "%REPO%\.deps\Qt\6.10.3\msvc2022_64\bin\qmake.exe" (
  echo The Qt host kit was not found at .deps\Qt\6.10.3\msvc2022_64 ^(needed for QT_HOST_PATH^).
  goto fail
)

if not exist "%REPO%\packaging\web\patchy.html.in" (
  echo The web shell template was not found: "%REPO%\packaging\web\patchy.html.in".
  goto fail
)

if not defined PATCHY_PACKAGE_VERSION (
  for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$text = Get-Content -Raw -LiteralPath 'CMakeLists.txt'; $match = [regex]::Match($text, 'project\s*\([\s\S]*?\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})', [Text.RegularExpressions.RegexOptions]::IgnoreCase); if ($match.Success) { $match.Groups[1].Value } else { '0.0.0' }"`) do set "PATCHY_PACKAGE_VERSION=%%V"
)
for /f "usebackq delims=" %%T in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMddHHmmss"`) do set "CACHE_TAG_STAMP=%%T"
set "PATCHY_WEB_CACHE_TAG=%PATCHY_PACKAGE_VERSION%-%CACHE_TAG_STAMP%"

if exist "%SITE_DIR%" rmdir /s /q "%SITE_DIR%"
if exist "%SITE_DIR%" goto fail

call "%REPO%\.deps\emsdk\emsdk_env.bat" >nul 2>&1
call "%REPO%\scripts\vs-env.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 goto fail

echo Configuring wasm-release build...
"%CMAKE_EXE%" --preset wasm-release
if errorlevel 1 goto fail

echo Building wasm-release preset...
"%CMAKE_EXE%" --build --preset wasm-release
if errorlevel 1 goto fail

echo Staging web site files...
mkdir "%SITE_DIR%" || goto fail
rem No separate pthread worker file: Emscripten 3.1.58+ folds the worker
rem bootstrap into patchy.js (the 3.1.56 builds staged patchy.worker.js here).
for %%F in (patchy.js patchy.wasm patchy.data qtloader.js) do (
  if not exist "%BUILD_DIR%\%%F" (
    echo %%F was not created in build\wasm-release.
    goto fail
  )
  copy /Y "%BUILD_DIR%\%%F" "%SITE_DIR%\" >nul || goto fail
)
copy /Y "%REPO%\packaging\linux\icons\hicolor\256x256\apps\com.rtsoft.patchy.png" "%SITE_DIR%\patchy-logo.png" >nul || goto fail
rem The browser-tab favicon is the app's own multi-size icon (16-256 px).
copy /Y "%REPO%\src\app\patchy.ico" "%SITE_DIR%\favicon.ico" >nul || goto fail
copy /Y "%REPO%\packaging\web\.htaccess" "%SITE_DIR%\.htaccess" >nul || goto fail
rem The shell page needs the uncompressed wasm size: its progress bar counts
rem decompressed bytes, which Content-Length cannot provide once the server
rem answers with a precompressed (br/gzip) variant.
for %%S in ("%SITE_DIR%\patchy.wasm") do set "PATCHY_WASM_SIZE=%%~zS"
copy /Y "%REPO%\NOTICE-THIRD-PARTY.md" "%SITE_DIR%\NOTICE-THIRD-PARTY.md" >nul || goto fail
copy /Y "%REPO%\src\formats\libheif\COPYING" "%SITE_DIR%\libheif-COPYING.txt" >nul || goto fail

rem Configure the shell template; it is staged as both patchy.html and an
rem index.html copy so https://rtsoft.com/patchy/ serves the app directly.
set "PATCHY_WEB_TEMPLATE=%REPO%\packaging\web\patchy.html.in"
set "PATCHY_WEB_SITE_DIR=%SITE_DIR%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$html = Get-Content -Raw -LiteralPath $env:PATCHY_WEB_TEMPLATE; $html = $html.Replace('__PATCHY_VERSION__', $env:PATCHY_PACKAGE_VERSION).Replace('__PATCHY_CACHE_TAG__', $env:PATCHY_WEB_CACHE_TAG).Replace('__PATCHY_WASM_SIZE__', $env:PATCHY_WASM_SIZE); if ($html -match '__PATCHY_') { Write-Error 'patchy.html.in still contains an unreplaced __PATCHY_ placeholder.'; exit 1 }; Set-Content -LiteralPath (Join-Path $env:PATCHY_WEB_SITE_DIR 'patchy.html') -Value $html -NoNewline -Encoding UTF8; Set-Content -LiteralPath (Join-Path $env:PATCHY_WEB_SITE_DIR 'index.html') -Value $html -NoNewline -Encoding UTF8"
if errorlevel 1 goto fail

rem Stage the memory-diagnostics harness with the same cache tag, plus the
rem soak script it fetches. Production uploads never ship these (their upload
rem list is explicit); the beta upload does (upload-wasm-to-rtsoft-beta.bat).
set "PATCHY_WEB_HARNESS=%REPO%\scripts\wasm\stress-harness.html"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$html = Get-Content -Raw -LiteralPath $env:PATCHY_WEB_HARNESS; $html = $html.Replace('__PATCHY_CACHE_TAG__', $env:PATCHY_WEB_CACHE_TAG); Set-Content -LiteralPath (Join-Path $env:PATCHY_WEB_SITE_DIR 'stress-harness.html') -Value $html -NoNewline -Encoding UTF8"
if errorlevel 1 goto fail
copy /Y "%REPO%\scripts\wasm\memsoak.js" "%SITE_DIR%\memsoak.js" >nul || goto fail

echo Precompressing site assets...
rem Brotli/gzip variants beside the identity files; the staged .htaccess
rem rewrites to them for clients that accept the encoding. The emsdk-bundled
rem node runs the compressor (no extra tool dependency); the scripts glob the
rem single node directory the SDK keeps (see docs/wasm.md).
set "NODE_EXE="
for /d %%D in ("%REPO%\.deps\emsdk\node\*") do set "NODE_EXE=%%D\bin\node.exe"
if not defined NODE_EXE (
  echo The emsdk-bundled node was not found under .deps\emsdk\node.
  goto fail
)
if not exist "%NODE_EXE%" (
  echo The emsdk-bundled node was not found: "%NODE_EXE%".
  goto fail
)
"%NODE_EXE%" "%REPO%\scripts\wasm\precompress-site.mjs" "%SITE_DIR%" || goto fail

echo Wasm site staged: "%SITE_DIR%" (cache tag %PATCHY_WEB_CACHE_TAG%)
echo Test it with scripts\release\start-local-wasm-server.bat, publish it with
echo scripts\release\upload-wasm-to-rtsoft.bat.
popd
exit /b 0

:fail
echo Wasm release build/staging failed.
popd
exit /b 1

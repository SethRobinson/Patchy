@echo off
setlocal EnableExtensions
rem Builds the wasm-release preset (the Qt for WebAssembly app; see docs/wasm.md)
rem and stages the deployable site files into build\package\wasm-site for
rem upload-wasm-to-rtsoft.bat. The staging directory is deleted up front, so a
rem failed build can never leave stale files behind for the upload script.
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
if not exist "%REPO%\.deps\Qt\6.8.3\wasm_singlethread\lib\cmake\Qt6\qt.toolchain.cmake" (
  echo Qt for WebAssembly was not found. Run: pwsh -File scripts\wasm\setup-qt-wasm.ps1
  goto fail
)
if not exist "%REPO%\.deps\Qt\6.8.3\msvc2022_64\bin\qmake.exe" (
  echo The Qt host kit was not found at .deps\Qt\6.8.3\msvc2022_64 ^(needed for QT_HOST_PATH^).
  goto fail
)

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
for %%F in (patchy.html patchy.js patchy.wasm patchy.data qtloader.js qtlogo.svg) do (
  if not exist "%BUILD_DIR%\%%F" (
    echo %%F was not created in build\wasm-release.
    goto fail
  )
  copy /Y "%BUILD_DIR%\%%F" "%SITE_DIR%\" >nul || goto fail
)
rem index.html copy so https://rtsoft.com/patchy/ serves the app without /patchy.html.
copy /Y "%BUILD_DIR%\patchy.html" "%SITE_DIR%\index.html" >nul || goto fail

echo Wasm site staged: "%SITE_DIR%"
echo Test it with scripts\release\start-local-wasm-server.bat, publish it with
echo scripts\release\upload-wasm-to-rtsoft.bat.
popd
exit /b 0

:fail
echo Wasm release build/staging failed.
popd
exit /b 1

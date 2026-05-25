@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"
pushd "%REPO%" || exit /b 1

set "VS_DEV=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"

set "APP_NAME=Photoslop"
set "SIGN_DISPLAY_NAME=Photoslop"
set "SIGN_DOMAIN=rtsoft.com"
set "SIGNTOOL_EXE=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"

set "QT_PREFIX=%PHOTOSLOP_QT_PREFIX%"
if not defined QT_PREFIX set "QT_PREFIX=%REPO%\.deps\Qt\6.8.3\msvc2022_64"
set "WINDEPLOYQT=%QT_PREFIX%\bin\windeployqt.exe"

if not exist "%WINDEPLOYQT%" (
  echo windeployqt.exe was not found at "%WINDEPLOYQT%".
  echo Set PHOTOSLOP_QT_PREFIX to a Qt 6 MSVC x64 install and run this script again.
  goto fail
)

if exist "%VS_DEV%" (
  call "%VS_DEV%" -arch=x64 -host_arch=x64 >nul
  if errorlevel 1 goto fail
) else (
  echo VsDevCmd.bat was not found at "%VS_DEV%"; using the current command environment.
)

if not defined PHOTOSLOP_PACKAGE_VERSION (
  for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$text = Get-Content -Raw -LiteralPath 'CMakeLists.txt'; $match = [regex]::Match($text, 'project\s*\([\s\S]*?\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})', [Text.RegularExpressions.RegexOptions]::IgnoreCase); if ($match.Success) { $match.Groups[1].Value } else { '0.0.0' }"`) do set "PHOTOSLOP_PACKAGE_VERSION=%%V"
)

set "BUILD_DIR=%REPO%\build\release"
set "PACKAGE_ROOT=%REPO%\build\package"
set "PACKAGE_NAME=photoslop-%PHOTOSLOP_PACKAGE_VERSION%-windows-x64"
set "STAGE_DIR=%PACKAGE_ROOT%\%PACKAGE_NAME%"
set "ZIP_PATH=%PACKAGE_ROOT%\%PACKAGE_NAME%.zip"
set "APP_EXE=%BUILD_DIR%\photoslop.exe"

echo Configuring release build...
"%CMAKE_EXE%" --preset release -DPHOTOSLOP_BUILD_APP=ON -DCMAKE_PREFIX_PATH="%QT_PREFIX%"
if errorlevel 1 goto fail

echo Building release preset...
"%CMAKE_EXE%" --build --preset release
if errorlevel 1 goto fail

if not exist "%APP_EXE%" (
  echo Release executable was not created at "%APP_EXE%".
  goto fail
)

echo Deploying Qt runtime...
"%WINDEPLOYQT%" --release "%APP_EXE%"
if errorlevel 1 goto fail

call :SignReleaseExe
if errorlevel 1 goto fail

if not exist "%PACKAGE_ROOT%" mkdir "%PACKAGE_ROOT%"
set "PHOTOSLOP_PACKAGE_ROOT=%PACKAGE_ROOT%"
set "PHOTOSLOP_STAGE_DIR=%STAGE_DIR%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$root = [IO.Path]::GetFullPath($env:PHOTOSLOP_PACKAGE_ROOT); $target = [IO.Path]::GetFullPath($env:PHOTOSLOP_STAGE_DIR); if (-not $target.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { Write-Error 'Refusing to clear staging directory outside the package root.'; exit 1 }"
if errorlevel 1 goto fail

if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
if exist "%STAGE_DIR%" goto fail
mkdir "%STAGE_DIR%" || goto fail

echo Staging runtime files...
copy /Y "%APP_EXE%" "%STAGE_DIR%\" >nul || goto fail
for %%F in ("%BUILD_DIR%\*.dll") do copy /Y "%%~fF" "%STAGE_DIR%\" >nul
if exist "%BUILD_DIR%\vc_redist.x64.exe" copy /Y "%BUILD_DIR%\vc_redist.x64.exe" "%STAGE_DIR%\" >nul

for %%D in (generic iconengines imageformats networkinformation platforms styles tls translations test-fixtures) do (
  if exist "%BUILD_DIR%\%%D" (
    robocopy "%BUILD_DIR%\%%D" "%STAGE_DIR%\%%D" /E /XF *d.dll qminimal.dll qoffscreen.dll /R:1 /W:1 /NFL /NDL /NJH /NJS /NP >nul
    if !ERRORLEVEL! GEQ 8 goto fail
  )
)

copy /Y "%REPO%\README.md" "%STAGE_DIR%\README.md" >nul
copy /Y "%REPO%\NOTICE-THIRD-PARTY.md" "%STAGE_DIR%\NOTICE-THIRD-PARTY.md" >nul
copy /Y "%REPO%\packaging\windows\README.md" "%STAGE_DIR%\WINDOWS-PACKAGING.md" >nul

echo Creating zip package...
set "PHOTOSLOP_ZIP_PATH=%ZIP_PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "if (Test-Path -LiteralPath $env:PHOTOSLOP_ZIP_PATH) { Remove-Item -LiteralPath $env:PHOTOSLOP_ZIP_PATH -Force }; Compress-Archive -Path (Join-Path $env:PHOTOSLOP_STAGE_DIR '*') -DestinationPath $env:PHOTOSLOP_ZIP_PATH -Force"
if errorlevel 1 goto fail

echo Release zip created: "%ZIP_PATH%"
popd
exit /b 0

:SignReleaseExe
if not defined RT_PROJECTS (
  echo RT_PROJECTS is not set; signing skipped.
  exit /b 0
)

if not exist "%RT_PROJECTS%\Signing\sign.bat" (
  echo Signing script was not found: "%RT_PROJECTS%\Signing\sign.bat"; signing skipped.
  exit /b 0
)

if not exist "%SIGNTOOL_EXE%" (
  echo SignTool was not found: "%SIGNTOOL_EXE%"
  exit /b 1
)

timeout /t 2 /nobreak >nul
call "%RT_PROJECTS%\Signing\sign.bat" "%APP_EXE%" "%SIGN_DISPLAY_NAME%" "%SIGN_DOMAIN%"
if errorlevel 1 exit /b %ERRORLEVEL%

"%SIGNTOOL_EXE%" verify /pa /v "%APP_EXE%"
exit /b %ERRORLEVEL%

:fail
echo Release build/package failed.
popd
exit /b 1

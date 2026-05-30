@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "REPO=%~dp0"
if "%REPO:~-1%"=="\" set "REPO=%REPO:~0,-1%"
pushd "%REPO%" || exit /b 1

set "VS_DEV=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"
set "CSC_EXE=%SystemRoot%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC_EXE%" set "CSC_EXE=csc.exe"

set "APP_NAME=Patchy"
set "SIGN_DISPLAY_NAME=Patchy"
set "SIGN_DOMAIN=rtsoft.com"
set "SIGNTOOL_EXE=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
set "IEXPRESS_EXE=%SystemRoot%\System32\iexpress.exe"
if not exist "%IEXPRESS_EXE%" set "IEXPRESS_EXE=iexpress.exe"

set "QT_PREFIX=%PATCHY_QT_PREFIX%"
if not defined QT_PREFIX set "QT_PREFIX=%REPO%\.deps\Qt\6.8.3\msvc2022_64"
set "WINDEPLOYQT=%QT_PREFIX%\bin\windeployqt.exe"
set "QTPATHS=%QT_PREFIX%\bin\qtpaths.exe"
if not exist "%QTPATHS%" set "QTPATHS=%QT_PREFIX%\bin\qtpaths6.exe"

if not exist "%WINDEPLOYQT%" (
  echo windeployqt.exe was not found at "%WINDEPLOYQT%".
  echo Set PATCHY_QT_PREFIX to a Qt 6 MSVC x64 install and run this script again.
  goto fail
)

if not exist "%QTPATHS%" (
  echo qtpaths.exe was not found under "%QT_PREFIX%\bin".
  echo Set PATCHY_QT_PREFIX to a complete Qt 6 MSVC x64 install and run this script again.
  goto fail
)

if exist "%VS_DEV%" (
  call "%VS_DEV%" -arch=x64 -host_arch=x64 >nul
  if errorlevel 1 goto fail
) else (
  echo VsDevCmd.bat was not found at "%VS_DEV%"; using the current command environment.
)

if not defined PATCHY_PACKAGE_VERSION (
  for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$text = Get-Content -Raw -LiteralPath 'CMakeLists.txt'; $match = [regex]::Match($text, 'project\s*\([\s\S]*?\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})', [Text.RegularExpressions.RegexOptions]::IgnoreCase); if ($match.Success) { $match.Groups[1].Value } else { '0.0.0' }"`) do set "PATCHY_PACKAGE_VERSION=%%V"
)

set "BUILD_DIR=%REPO%\build\release"
set "PACKAGE_ROOT=%REPO%\build\package"
set "PACKAGE_NAME=Patchy"
set "STAGE_DIR=%PACKAGE_ROOT%\staging\%PACKAGE_NAME%"
set "ZIP_PATH=%PACKAGE_ROOT%\PatchyWindows.zip"
set "INSTALLER_PATH=%PACKAGE_ROOT%\PatchyWindowsInstaller.exe"
set "INSTALLER_WORK_DIR=%PACKAGE_ROOT%\installer"
set "INSTALLER_PAYLOAD_DIR=%INSTALLER_WORK_DIR%\payload"
set "INSTALLER_SED_PATH=%INSTALLER_WORK_DIR%\PatchyWindowsInstaller.sed"
set "WINDOWS_PACKAGING_DIR=%REPO%\packaging\windows"
set "APP_ICON=%REPO%\src\app\patchy.ico"
set "APP_EXE=%BUILD_DIR%\patchy.exe"

echo Configuring release build...
"%CMAKE_EXE%" --preset release -DPATCHY_BUILD_APP=ON -DCMAKE_PREFIX_PATH="%QT_PREFIX%"
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
set "PATCHY_PACKAGE_ROOT=%PACKAGE_ROOT%"
set "PATCHY_STAGE_DIR=%STAGE_DIR%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$root = [IO.Path]::GetFullPath($env:PATCHY_PACKAGE_ROOT); $target = [IO.Path]::GetFullPath($env:PATCHY_STAGE_DIR); if (-not $target.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { Write-Error 'Refusing to clear staging directory outside the package root.'; exit 1 }"
if errorlevel 1 goto fail

if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
if exist "%STAGE_DIR%" goto fail
mkdir "%STAGE_DIR%" || goto fail

echo Staging application and Qt runtime...
copy /Y "%APP_EXE%" "%STAGE_DIR%\" >nul || goto fail
"%WINDEPLOYQT%" --release ^
  --dir "%STAGE_DIR%" ^
  --no-compiler-runtime ^
  --no-translations ^
  --no-system-d3d-compiler ^
  --no-system-dxc-compiler ^
  --no-opengl-sw ^
  --exclude-plugins "qtuiotouchplugin,qminimal,qoffscreen,qdirect2d,qgif,qico,qicns,qtga,qwbmp" ^
  "%STAGE_DIR%\patchy.exe"
if errorlevel 1 goto fail

for %%F in (
  "%STAGE_DIR%\imageformats\qgif.dll"
  "%STAGE_DIR%\imageformats\qicns.dll"
  "%STAGE_DIR%\imageformats\qico.dll"
  "%STAGE_DIR%\imageformats\qtga.dll"
  "%STAGE_DIR%\imageformats\qwbmp.dll"
) do (
  if exist "%%~fF" del /q "%%~fF"
)
if exist "%STAGE_DIR%\vc_redist.x64.exe" del /q "%STAGE_DIR%\vc_redist.x64.exe"

call :CopyRequiredImageFormatPlugins
if errorlevel 1 goto fail

call :CopyRequiredTlsPlugins
if errorlevel 1 goto fail

call :CopyMsvcRuntimeDlls
if errorlevel 1 goto fail

call :CopyTranslations
if errorlevel 1 goto fail

call :CopyBundledFonts
if errorlevel 1 goto fail

copy /Y "%REPO%\README.md" "%STAGE_DIR%\README.md" >nul
copy /Y "%REPO%\NOTICE-THIRD-PARTY.md" "%STAGE_DIR%\NOTICE-THIRD-PARTY.md" >nul
copy /Y "%REPO%\LICENSE" "%STAGE_DIR%\LICENSE" >nul || goto fail
copy /Y "%APP_ICON%" "%STAGE_DIR%\Patchy.ico" >nul || goto fail

if not exist "%WINDOWS_PACKAGING_DIR%\UninstallPatchy.cs" (
  echo Installer uninstaller source was not found: "%WINDOWS_PACKAGING_DIR%\UninstallPatchy.cs".
  goto fail
)
"%CSC_EXE%" /nologo /target:winexe /platform:x64 /optimize+ /win32icon:"%APP_ICON%" /out:"%STAGE_DIR%\UninstallPatchy.exe" "%WINDOWS_PACKAGING_DIR%\UninstallPatchy.cs"
if errorlevel 1 goto fail

call :CopyQtLicenseSbom
if errorlevel 1 goto fail

echo Writing install manifest...
set "PATCHY_INSTALL_MANIFEST=%STAGE_DIR%\PatchyInstallManifest.txt"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$stage = [IO.Path]::GetFullPath($env:PATCHY_STAGE_DIR).TrimEnd('\') + '\'; $manifest = [IO.Path]::GetFullPath($env:PATCHY_INSTALL_MANIFEST); $files = Get-ChildItem -LiteralPath $env:PATCHY_STAGE_DIR -Recurse -File | Where-Object { [IO.Path]::GetFullPath($_.FullName) -ne $manifest } | ForEach-Object { $_.FullName.Substring($stage.Length) } | Sort-Object; $files = @($files) + 'PatchyInstallManifest.txt'; Set-Content -LiteralPath $manifest -Value $files -Encoding ASCII"
if errorlevel 1 goto fail

echo Creating zip package...
set "PATCHY_ZIP_PATH=%ZIP_PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "if (Test-Path -LiteralPath $env:PATCHY_ZIP_PATH) { Remove-Item -LiteralPath $env:PATCHY_ZIP_PATH -Force }; Compress-Archive -Path $env:PATCHY_STAGE_DIR -DestinationPath $env:PATCHY_ZIP_PATH -Force"
if errorlevel 1 goto fail

echo Release zip created: "%ZIP_PATH%"

echo Creating Windows installer...
call :CreateWindowsInstaller
if errorlevel 1 goto fail

call :SignInstaller
if errorlevel 1 goto fail

echo Release installer created: "%INSTALLER_PATH%"
popd
exit /b 0

:SignReleaseExe
call :SignFile "%APP_EXE%"
exit /b %ERRORLEVEL%

:SignInstaller
call :SignFile "%INSTALLER_PATH%"
exit /b %ERRORLEVEL%

:SignFile
set "SIGN_TARGET=%~1"
if not defined RT_PROJECTS (
  echo RT_PROJECTS is not set; signing skipped for "%SIGN_TARGET%".
  exit /b 0
)

if not exist "%RT_PROJECTS%\Signing\sign.bat" (
  echo Signing script was not found: "%RT_PROJECTS%\Signing\sign.bat"; signing skipped for "%SIGN_TARGET%".
  exit /b 0
)

if not exist "%SIGNTOOL_EXE%" (
  echo SignTool was not found: "%SIGNTOOL_EXE%"
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Sleep -Seconds 2"
call "%RT_PROJECTS%\Signing\sign.bat" "%SIGN_TARGET%" "%SIGN_DISPLAY_NAME%" "%SIGN_DOMAIN%"
if errorlevel 1 exit /b %ERRORLEVEL%

"%SIGNTOOL_EXE%" verify /pa /v "%SIGN_TARGET%"
exit /b %ERRORLEVEL%

:CopyRequiredImageFormatPlugins
set "PATCHY_STAGE_IMAGEFORMATS=%STAGE_DIR%\imageformats"
if not exist "%PATCHY_STAGE_IMAGEFORMATS%" mkdir "%PATCHY_STAGE_IMAGEFORMATS%" || exit /b 1
for %%P in (qjpeg qsvg qtiff qwebp) do (
  if not exist "%QT_PREFIX%\plugins\imageformats\%%P.dll" (
    echo Required Qt image format plugin was not found: "%QT_PREFIX%\plugins\imageformats\%%P.dll".
    echo Install the Qt qtimageformats module and run this script again.
    exit /b 1
  )
  copy /Y "%QT_PREFIX%\plugins\imageformats\%%P.dll" "%PATCHY_STAGE_IMAGEFORMATS%\" >nul || exit /b 1
)
exit /b 0

:CopyRequiredTlsPlugins
set "PATCHY_STAGE_TLS=%STAGE_DIR%\tls"
if not exist "%PATCHY_STAGE_TLS%" mkdir "%PATCHY_STAGE_TLS%" || exit /b 1
for %%P in (qschannelbackend) do (
  if not exist "%QT_PREFIX%\plugins\tls\%%P.dll" (
    echo Required Qt TLS plugin was not found: "%QT_PREFIX%\plugins\tls\%%P.dll".
    exit /b 1
  )
  copy /Y "%QT_PREFIX%\plugins\tls\%%P.dll" "%PATCHY_STAGE_TLS%\" >nul || exit /b 1
)
exit /b 0

:CopyMsvcRuntimeDlls
if not defined VCINSTALLDIR (
  echo VCINSTALLDIR is not set; cannot locate app-local MSVC runtime DLLs.
  exit /b 1
)

set "MSVC_REDIST_ROOT=%VCINSTALLDIR%Redist\MSVC"
if not exist "%MSVC_REDIST_ROOT%" (
  echo MSVC redistributable root was not found: "%MSVC_REDIST_ROOT%".
  exit /b 1
)

set "PATCHY_MSVC_REDIST_ROOT=%MSVC_REDIST_ROOT%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$root = [IO.Path]::GetFullPath($env:PATCHY_MSVC_REDIST_ROOT); $stage = [IO.Path]::GetFullPath($env:PATCHY_STAGE_DIR); $crt = Get-ChildItem -LiteralPath $root -Directory | ForEach-Object { Get-ChildItem -Path (Join-Path $_.FullName 'x64\Microsoft.VC*.CRT') -Directory -ErrorAction SilentlyContinue } | Sort-Object FullName -Descending | Select-Object -First 1; if ($null -eq $crt) { Write-Error ('No x64 Microsoft.VC*.CRT directory found under ' + $root); exit 1 }; $files = Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' -File | Sort-Object Name; if ($files.Count -eq 0) { Write-Error ('No MSVC runtime DLLs found under ' + $crt.FullName); exit 1 }; foreach ($file in $files) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $stage $file.Name) -Force }"
exit /b %ERRORLEVEL%

:CopyTranslations
set "PATCHY_BUILD_TRANSLATIONS=%BUILD_DIR%\translations"
set "PATCHY_STAGE_TRANSLATIONS=%STAGE_DIR%\translations"
if not exist "%PATCHY_BUILD_TRANSLATIONS%\patchy_ja.qm" (
  echo Patchy Japanese translation was not found: "%PATCHY_BUILD_TRANSLATIONS%\patchy_ja.qm".
  exit /b 1
)
if not exist "%QT_PREFIX%\translations\qtbase_ja.qm" (
  echo Qt Japanese base translation was not found: "%QT_PREFIX%\translations\qtbase_ja.qm".
  exit /b 1
)
if not exist "%PATCHY_STAGE_TRANSLATIONS%" mkdir "%PATCHY_STAGE_TRANSLATIONS%" || exit /b 1
copy /Y "%PATCHY_BUILD_TRANSLATIONS%\patchy_ja.qm" "%PATCHY_STAGE_TRANSLATIONS%\" >nul || exit /b 1
copy /Y "%QT_PREFIX%\translations\qtbase_ja.qm" "%PATCHY_STAGE_TRANSLATIONS%\" >nul || exit /b 1
exit /b 0

:CopyBundledFonts
set "PATCHY_BUNDLED_FONTS=%REPO%\third_party\fonts"
set "PATCHY_STAGE_FONTS=%STAGE_DIR%\fonts"
if not exist "%PATCHY_BUNDLED_FONTS%" (
  echo Bundled font directory was not found: "%PATCHY_BUNDLED_FONTS%".
  exit /b 1
)
if not exist "%PATCHY_STAGE_FONTS%" mkdir "%PATCHY_STAGE_FONTS%" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -Command "Copy-Item -Path (Join-Path $env:PATCHY_BUNDLED_FONTS '*') -Destination $env:PATCHY_STAGE_FONTS -Recurse -Force"
exit /b %ERRORLEVEL%

:CopyQtLicenseSbom
for /f "usebackq delims=" %%V in (`"%QTPATHS%" --qt-version`) do set "QT_VERSION=%%V"
if not defined QT_VERSION (
  echo Could not determine the Qt version from "%QTPATHS%".
  exit /b 1
)

set "QT_SBOM_DIR=%QT_PREFIX%\sbom"
set "QT_LICENSE_DIR=%STAGE_DIR%\licenses\qt"
if not exist "%QT_LICENSE_DIR%" mkdir "%QT_LICENSE_DIR%" || exit /b 1

for %%M in (qtbase qtimageformats qtsvg) do (
  set "QT_SBOM_FILE=%QT_SBOM_DIR%\%%M-%QT_VERSION%.spdx"
  if not exist "!QT_SBOM_FILE!" (
    echo Required Qt license SBOM was not found: "!QT_SBOM_FILE!".
    exit /b 1
  )
  copy /Y "!QT_SBOM_FILE!" "%QT_LICENSE_DIR%\" >nul || exit /b 1
)
exit /b 0

:CreateWindowsInstaller
if not exist "%IEXPRESS_EXE%" (
  echo iexpress.exe was not found. It is required to create "%INSTALLER_PATH%".
  exit /b 1
)

if not exist "%WINDOWS_PACKAGING_DIR%\InstallPatchyLauncher.cs" (
  echo Installer launcher source was not found: "%WINDOWS_PACKAGING_DIR%\InstallPatchyLauncher.cs".
  exit /b 1
)

if not exist "%WINDOWS_PACKAGING_DIR%\InstallPatchy.ps1" (
  echo Installer PowerShell template was not found: "%WINDOWS_PACKAGING_DIR%\InstallPatchy.ps1".
  exit /b 1
)

if not exist "%WINDOWS_PACKAGING_DIR%\PatchyWindowsInstaller.sed.in" (
  echo IExpress template was not found: "%WINDOWS_PACKAGING_DIR%\PatchyWindowsInstaller.sed.in".
  exit /b 1
)

if not exist "%APP_ICON%" (
  echo Application icon was not found: "%APP_ICON%".
  exit /b 1
)

set "PATCHY_INSTALLER_WORK_DIR=%INSTALLER_WORK_DIR%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$root = [IO.Path]::GetFullPath($env:PATCHY_PACKAGE_ROOT); $target = [IO.Path]::GetFullPath($env:PATCHY_INSTALLER_WORK_DIR); if (-not $target.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { Write-Error 'Refusing to clear installer work directory outside the package root.'; exit 1 }"
if errorlevel 1 exit /b %ERRORLEVEL%

if exist "%INSTALLER_WORK_DIR%" rmdir /s /q "%INSTALLER_WORK_DIR%"
if exist "%INSTALLER_WORK_DIR%" exit /b 1
mkdir "%INSTALLER_PAYLOAD_DIR%" || exit /b 1

set "PATCHY_INSTALLER_PAYLOAD_DIR=%INSTALLER_PAYLOAD_DIR%\"
copy /Y "%ZIP_PATH%" "%INSTALLER_PAYLOAD_DIR%\PatchyWindows.zip" >nul || exit /b 1
copy /Y "%WINDOWS_PACKAGING_DIR%\InstallPatchy.ps1" "%INSTALLER_PAYLOAD_DIR%\InstallPatchy.ps1" >nul || exit /b 1
copy /Y "%APP_ICON%" "%INSTALLER_PAYLOAD_DIR%\Patchy.ico" >nul || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Content -LiteralPath (Join-Path $env:PATCHY_INSTALLER_PAYLOAD_DIR 'PatchyVersion.txt') -Value $env:PATCHY_PACKAGE_VERSION -Encoding ASCII"
if errorlevel 1 exit /b %ERRORLEVEL%

"%CSC_EXE%" /nologo /target:winexe /platform:x64 /optimize+ /win32icon:"%APP_ICON%" /reference:System.Windows.Forms.dll /out:"%INSTALLER_PAYLOAD_DIR%\InstallPatchy.exe" "%WINDOWS_PACKAGING_DIR%\InstallPatchyLauncher.cs"
if errorlevel 1 exit /b %ERRORLEVEL%

set "PATCHY_INSTALLER_SED_TEMPLATE=%WINDOWS_PACKAGING_DIR%\PatchyWindowsInstaller.sed.in"
set "PATCHY_INSTALLER_SED_PATH=%INSTALLER_SED_PATH%"
set "PATCHY_INSTALLER_PATH=%INSTALLER_PATH%"
set "PATCHY_APP_ICON_PATH=%APP_ICON%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$sed = Get-Content -Raw -LiteralPath $env:PATCHY_INSTALLER_SED_TEMPLATE; $sed = $sed.Replace('__INSTALLER_PATH__', $env:PATCHY_INSTALLER_PATH).Replace('__PAYLOAD_DIR__', $env:PATCHY_INSTALLER_PAYLOAD_DIR).Replace('__APP_ICON_PATH__', $env:PATCHY_APP_ICON_PATH); Set-Content -LiteralPath $env:PATCHY_INSTALLER_SED_PATH -Value $sed -Encoding ASCII"
if errorlevel 1 exit /b %ERRORLEVEL%

if exist "%INSTALLER_PATH%" del /q "%INSTALLER_PATH%"
set "PATCHY_IEXPRESS_EXE=%IEXPRESS_EXE%"
set "PATCHY_INSTALLER_SED_RUN_PATH=%INSTALLER_SED_PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$process = Start-Process -FilePath $env:PATCHY_IEXPRESS_EXE -ArgumentList @('/N', '/Q', $env:PATCHY_INSTALLER_SED_RUN_PATH) -Wait -PassThru; exit $process.ExitCode"
set "IEXPRESS_EXIT_CODE=%ERRORLEVEL%"

set "PATCHY_INSTALLER_WAIT_PATH=%INSTALLER_PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$path = $env:PATCHY_INSTALLER_WAIT_PATH; $deadline = (Get-Date).AddSeconds(120); $lastSize = -1L; while ((Get-Date) -lt $deadline) { if (Test-Path -LiteralPath $path -PathType Leaf) { $size = (Get-Item -LiteralPath $path).Length; if ($size -gt 0 -and $size -eq $lastSize) { exit 0 }; $lastSize = $size }; Start-Sleep -Milliseconds 500 }; Write-Error ('IExpress completed, but the installer was not created at a stable nonzero size: ' + $path); exit 1"
if errorlevel 1 exit /b %ERRORLEVEL%
if not "%IEXPRESS_EXIT_CODE%"=="0" echo IExpress returned exit code %IEXPRESS_EXIT_CODE% after creating "%INSTALLER_PATH%"; continuing.
exit /b 0

:fail
echo Release build/package failed.
popd
exit /b 1

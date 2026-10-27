@echo off
rem Builds a legacy plug-in host (patchy-8bf-host32.exe for x86, patchy-8bf-host64.exe for
rem x64) as its own Ninja project inside that architecture's developer environment. Called
rem by the root CMakeLists.txt custom commands; not meant to be run by hand.
rem
rem   build-8bf-host.bat <source dir> <build dir> <output dir> <cmake.exe> <config> <ninja.exe> <x86|x64>
rem
rem An x64 build of Patchy builds only the 32-bit host this way. A Windows ARM64 build
rem builds both, because plug-ins are x86 or x64 code that an ARM64 process cannot load:
rem the x64 host runs under Windows' x64 emulation (docs/plugin-host.md). The compilers
rem are the ones native to the build machine (Hostarm64 on an ARM64 box).
rem
rem setlocal keeps VsDevCmd's environment out of the calling build.
setlocal
set "PATCHY_HOST_ARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "PATCHY_HOST_ARCH=arm64"
set "PATCHY_8BF_NAME=patchy-8bf-host32"
if /i "%~7"=="x64" set "PATCHY_8BF_NAME=patchy-8bf-host64"
call "%~dp0vs-env.bat" -arch=%~7 -host_arch=%PATCHY_HOST_ARCH% >nul
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
"%~4" -S "%~1" -B "%~2" -G Ninja "-DCMAKE_MAKE_PROGRAM=%~6" "-DCMAKE_BUILD_TYPE=%~5" -DPATCHY_8BF_HOST_NAME=%PATCHY_8BF_NAME% "-DPATCHY_8BF_HOST_OUTPUT_DIR=%~3"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
"%~4" --build "%~2"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
if not exist "%~3\%PATCHY_8BF_NAME%.exe" exit /b 1
exit /b 0

@echo off
rem Builds the 32-bit legacy plug-in host (patchy-8bf-host32.exe) as its own Ninja
rem project inside the x86 developer environment. Called by the root CMakeLists.txt
rem custom command; not meant to be run by hand.
rem
rem   build-8bf-host32.bat <source dir> <build dir> <output dir> <cmake.exe> <config> <ninja.exe>
rem
rem setlocal keeps VsDevCmd's x86 environment out of the calling (x64) build.
setlocal
call "%~dp0vs-env.bat" -arch=x86 -host_arch=x64 >nul
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
"%~4" -S "%~1" -B "%~2" -G Ninja "-DCMAKE_MAKE_PROGRAM=%~6" "-DCMAKE_BUILD_TYPE=%~5" -DPATCHY_8BF_HOST_NAME=patchy-8bf-host32 "-DPATCHY_8BF_HOST_OUTPUT_DIR=%~3"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
"%~4" --build "%~2"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
if not exist "%~3\patchy-8bf-host32.exe" exit /b 1
exit /b 0

@echo off
rem Enters the Visual Studio x64 developer environment in the CALLING cmd session,
rem forwarding every argument to VsDevCmd.bat:
rem
rem   scripts\vs-env.bat -arch=x64 -host_arch=x64 >nul && cmake --build --preset release
rem
rem No setlocal here on purpose: the point of the script is to leave VsDevCmd's
rem variables behind for whoever called it.
rem
rem The VS Installer directory goes on PATH first. VsDevCmd.bat reads the product
rem version by pushd-ing into that directory and running vswhere.exe by bare name, and
rem cmd refuses to resolve a bare name from the current directory when
rem NoDefaultCurrentDirectoryInExePath is set, which non-interactive shells do set.
rem VsDevCmd treats that probe as optional and still returns 0, but it prints
rem "'vswhere.exe' is not recognized", which reads like a build failure in a release
rem log. See docs/release-process.md.
rem
rem The dev box has VS Community 2026 at the fixed path below. Other machines (the
rem Windows offload box's VS 2026 Build Tools, see docs/platform.md) are found through
rem vswhere instead. The
rem lookup uses goto rather than an if-block because %ProgramFiles(x86)%'s closing
rem parenthesis would end a parenthesized block early.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set "PATCHY_VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if exist "%PATCHY_VS_DEV_CMD%" goto :found
for /f "usebackq delims=" %%i in (`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "PATCHY_VS_DEV_CMD=%%i\Common7\Tools\VsDevCmd.bat"
if exist "%PATCHY_VS_DEV_CMD%" goto :found
rem stderr, so a caller that redirects stdout to nul still sees this.
echo VsDevCmd.bat was not found (no VS 2026 Community, and vswhere found no VC tools); using the current command environment.>&2
exit /b 0
:found
call "%PATCHY_VS_DEV_CMD%" %*
exit /b %ERRORLEVEL%

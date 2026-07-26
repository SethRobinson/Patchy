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
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
set "PATCHY_VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%PATCHY_VS_DEV_CMD%" (
  rem stderr, so a caller that redirects stdout to nul still sees this.
  echo VsDevCmd.bat was not found at "%PATCHY_VS_DEV_CMD%"; using the current command environment.>&2
  exit /b 0
)
call "%PATCHY_VS_DEV_CMD%" %*
exit /b %ERRORLEVEL%

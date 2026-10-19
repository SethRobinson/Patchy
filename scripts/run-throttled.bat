@echo off
rem Runs one command at below-normal priority and returns ITS exit code:
rem
rem   scripts\run-throttled.bat "C:\...\cmake.exe" --build --preset release -j 20
rem   scripts\run-throttled.bat .\patchy_core_tests.exe
rem
rem Use this instead of writing `start "" /b /wait /belownormal ...` inline. That
rem form throttles correctly but LOSES the exit code: `start /wait` does set
rem ERRORLEVEL from the child, yet `cmd /c start ...` exits with start's own status,
rem which is 0 whenever start managed to launch the program. A test suite that
rem failed, or a build that did not link, then looks exactly like success
rem (September 2026: a full UI suite reported exit 0 while printing [FAIL]).
rem
rem Reading %ERRORLEVEL% on its own line, after start has returned, gives the
rem child's real code. Do not "simplify" this to `& if errorlevel 1 exit /b 1`:
rem that comparison is false for the negative codes a crash produces, so an
rem access violation (-1073741819) would be reported as success.
rem
rem A batch file must go through `cmd /c`: `start` runs a .bat/.cmd argument under
rem `cmd /K`, which stays at an interactive prompt after the batch ends (with /b it
rem sits invisibly on the caller's console, forever when stdin is a terminal) and
rem reports exit 0 for a failed build once stdin hits EOF (September 25, 2026: a
rem failed build-wasm.bat parked a remote session for two hours this way). The
rem wrapper below applies it, so callers pass the batch file as-is.
rem
rem A `cmake --build` (any argument is exactly --build) runs under the machine-wide build
rem lock: several AI sessions share the dev box, and a second build beside the first slows
rem both. build-busy-check.ps1 takes a named mutex for the whole build, then runs this file
rem again with the same arguments and PATCHY_BUILD_LOCK_HELD=1, which starts the build below.
rem When another build holds the lock (or a build that bypassed it is compiling) it exits 3
rem without building and says what is running and how to build on the offload host instead.
rem There is no gap between the check and the start for a second build to slip through.
rem The argument line travels in PATCHY_THROTTLED_ARGS. PATCHY_ALLOW_CONCURRENT_BUILD=1
rem skips the lock. `shift /1` keeps %0 (this script's path) for %~dp0, and %* is never
rem affected by shift, so the command runs unchanged.
rem
rem No setlocal: %ERRORLEVEL% must survive. The two variables are cleared again here.
if "%~1"=="" (
  echo usage: scripts\run-throttled.bat ^<command^> [args...]>&2
  exit /b 2
)
if /i "%~x1"==".bat" goto batch
if /i "%~x1"==".cmd" goto batch
:scan
if "%~1"=="" goto run
if "%~1"=="--build" goto busy_check
shift /1
goto scan

:busy_check
if "%PATCHY_BUILD_LOCK_HELD%"=="1" (
  set "PATCHY_BUILD_LOCK_HELD="
  goto run
)
if "%PATCHY_ALLOW_CONCURRENT_BUILD%"=="1" goto run
set "PATCHY_THROTTLED_ARGS=%*"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-busy-check.ps1"
rem %ERRORLEVEL% expands when this line is read, before set runs.
set "PATCHY_THROTTLED_ARGS=" & exit /b %ERRORLEVEL%

:run
start "" /b /wait /belownormal %*
exit /b %ERRORLEVEL%

:batch
start "" /b /wait /belownormal cmd /s /c "%*"
exit /b %ERRORLEVEL%

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
rem No setlocal: nothing here needs to be undone, and %ERRORLEVEL% must survive.
if "%~1"=="" (
  echo usage: scripts\run-throttled.bat ^<command^> [args...]>&2
  exit /b 2
)
if /i "%~x1"==".bat" goto batch
if /i "%~x1"==".cmd" goto batch
start "" /b /wait /belownormal %*
exit /b %ERRORLEVEL%

:batch
start "" /b /wait /belownormal cmd /s /c "%*"
exit /b %ERRORLEVEL%

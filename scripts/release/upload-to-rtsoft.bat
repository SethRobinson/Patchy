@echo off
rem Uploads every platform's release artifacts (stable "latest" names), plus the
rem wasm site to rtsoft.com/patchy. Per-platform: upload-windows-to-rtsoft.bat /
rem upload-mac-to-rtsoft.bat / upload-linux-to-rtsoft.bat /
rem upload-wasm-to-rtsoft.bat (each also runs standalone).
rem cd to the repo root (this script lives in scripts\release) so the relative
rem build\package paths in the per-platform scripts resolve no matter the launch
rem cwd, and call the siblings by full path: cmd won't search the current directory
rem for them when NoDefaultCurrentDirectoryInExePath is set (e.g. a non-interactive
rem shell).
cd /d "%~dp0..\.."
call "%~dp0upload-windows-to-rtsoft.bat" nopause
call "%~dp0upload-mac-to-rtsoft.bat" nopause
call "%~dp0upload-linux-to-rtsoft.bat" nopause
call "%~dp0upload-wasm-to-rtsoft.bat" nopause
pause

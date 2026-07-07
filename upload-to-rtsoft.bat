@echo off
rem Uploads every platform's release artifacts (stable "latest" names).
rem Per-platform: upload-windows-to-rtsoft.bat / upload-mac-to-rtsoft.bat /
rem upload-linux-to-rtsoft.bat (each also runs standalone).
rem cd to this script's own directory so the relative build\package paths in the
rem per-platform scripts resolve no matter the launch cwd, and call the siblings by
rem full path: cmd won't search the current directory for them when
rem NoDefaultCurrentDirectoryInExePath is set (e.g. a non-interactive shell).
cd /d "%~dp0"
call "%~dp0upload-windows-to-rtsoft.bat" nopause
call "%~dp0upload-mac-to-rtsoft.bat" nopause
call "%~dp0upload-linux-to-rtsoft.bat" nopause
pause

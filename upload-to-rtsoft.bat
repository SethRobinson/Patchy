@echo off
rem Uploads every platform's release artifacts (stable "latest" names).
rem Per-platform: upload-windows-to-rtsoft.bat / upload-mac-to-rtsoft.bat /
rem upload-linux-to-rtsoft.bat (each also runs standalone).
call upload-windows-to-rtsoft.bat nopause
call upload-mac-to-rtsoft.bat nopause
call upload-linux-to-rtsoft.bat nopause
pause

@echo off
set PATH=%~dp0..\scripts;%PATH%
cd %~dp0

call python sfen_check.py

pause

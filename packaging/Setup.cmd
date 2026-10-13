@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Install-Portable.ps1"
if errorlevel 1 echo Setup failed. Review the message above.
pause

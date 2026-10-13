@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Install-MediaTools.ps1"
if errorlevel 1 echo Video tools could not be installed. Review the message above.
pause

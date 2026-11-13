@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0.portable\Build.ps1" %*
if errorlevel 1 pause

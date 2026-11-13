@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0.portable\Start.ps1"
if errorlevel 1 pause

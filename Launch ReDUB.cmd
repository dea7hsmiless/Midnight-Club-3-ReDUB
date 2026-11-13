@echo off
cd /d "%~dp0"
if not exist "ReDUB Launcher.exe" powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0.portable\Build-Launcher.ps1"
if not exist "ReDUB Launcher.exe" (pause & exit /b 1)
start "" "%~dp0ReDUB Launcher.exe"

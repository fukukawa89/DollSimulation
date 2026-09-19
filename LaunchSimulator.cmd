@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Scripts\StartSimulator.ps1"
if errorlevel 1 pause

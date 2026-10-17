@echo off
rem Desbloqueia e executa o Pumpy.exe sem avisos do Windows SmartScreen
powershell -NoProfile -ExecutionPolicy Bypass -Command "Unblock-File -Path '%~dp0Pumpy.exe' -ErrorAction SilentlyContinue"
start "" "%~dp0Pumpy.exe"

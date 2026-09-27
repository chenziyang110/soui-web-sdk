@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\soui-input\run-current-smoke.ps1 -Transfer D:\soui-input
exit /b %errorlevel%

@echo off
set "SCRATCH_EXE=%~dp0..\..\out\scratch-x64\bin\Release\SouiScratch\SouiScratch.exe"
if not exist "%SCRATCH_EXE%" (
  echo Please build the demo first: powershell -File "%~dp0Build.ps1"
  pause
  exit /b 1
)
start "" /D "%~dp0..\..\out\scratch-x64\bin\Release\SouiScratch" "%SCRATCH_EXE%"

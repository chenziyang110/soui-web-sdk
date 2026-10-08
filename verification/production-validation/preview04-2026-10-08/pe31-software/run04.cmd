@echo off
setlocal
set "SRC=%~dp0"
set "DEST=%SystemDrive%\soui04-validation"
if not exist "%DEST%" mkdir "%DEST%"
ver > "%DEST%\os.txt"
echo PROCESSOR_ARCHITECTURE=%PROCESSOR_ARCHITECTURE% >> "%DEST%\os.txt"
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT > "%DEST%\minint.txt" 2>&1
"%SRC%7za.exe" x -y "-o%DEST%" "%SRC%compatibility.zip" > "%DEST%\extract.txt" 2>&1
set "EXTRACT_EXIT=%ERRORLEVEL%"
echo %EXTRACT_EXIT% > "%DEST%\extract-exit.txt"
if not "%EXTRACT_EXIT%"=="0" goto Export
cd /d "%DEST%"
SouiRuntimeDoctor.exe > doctor.json 2> doctor-error.txt
echo %ERRORLEVEL% > doctor-exit.txt
SouiStarter.exe --smoke-test > app-stdout.txt 2> app-stderr.txt
echo %ERRORLEVEL% > app-exit.txt
:Export
mode COM1: BAUD=115200 PARITY=N DATA=8 STOP=1 > nul 2>&1
echo SOUI04_SERIAL_START > COM1
for %%f in (os.txt minint.txt extract-exit.txt doctor.json doctor-error.txt doctor-exit.txt app-exit.txt app-stderr.txt smoke.log) do (
  echo FILE:%%f > COM1
  if exist "%DEST%\%%f" type "%DEST%\%%f" > COM1
  echo END_FILE > COM1
)
echo SOUI04_SERIAL_END > COM1
echo SOUI 0.4 validation complete. Results: %DEST%
pause

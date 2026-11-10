@echo off
setlocal
cd /d "%~dp0"

set "DLL=%CD%\build\xinput1_4.dll"
set "GAME=%CD%\..\Game"

if not exist "%DLL%" (
  echo [SprintSplit] build\xinput1_4.dll not found, run build.bat first.
  exit /b 1
)
if not exist "%GAME%\eldenring.exe" (
  echo [SprintSplit] Game folder not found: %GAME%
  exit /b 1
)

copy /y "%DLL%" "%GAME%\xinput1_4.dll" >nul
if not exist "%GAME%\SprintSplit" mkdir "%GAME%\SprintSplit"
if not exist "%GAME%\SprintSplit\SprintSplit.ini" (
  copy /y "SprintSplit.ini" "%GAME%\SprintSplit\SprintSplit.ini" >nul
)

echo [SprintSplit] Installed:
echo   %GAME%\xinput1_4.dll
echo   %GAME%\SprintSplit\SprintSplit.ini
echo [SprintSplit] Start the game the way you normally do, then check
echo   %GAME%\SprintSplit\SprintSplit.log
exit /b 0

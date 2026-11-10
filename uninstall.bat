@echo off
setlocal
cd /d "%~dp0"
set "GAME=%CD%\..\Game"

if exist "%GAME%\xinput1_4.dll" del /q "%GAME%\xinput1_4.dll"
if exist "%GAME%\SprintSplit" rd /s /q "%GAME%\SprintSplit"
echo [SprintSplit] Removed from %GAME%
exit /b 0

@echo off
setlocal
cd /d "%~dp0"

if not exist "build" mkdir "build"
set "OUT=%CD%\build\xinput1_4.dll"
if exist "%OUT%" del /q "%OUT%"

call :try_msvc
if exist "%OUT%" goto built
call :try_mingw
if exist "%OUT%" goto built
call :try_zig
if exist "%OUT%" goto built

echo.
echo [SprintSplit] No C++ compiler found.
echo   1^) Install "Visual Studio Build Tools" with "Desktop development with C++"
echo   2^) or put MinGW-w64 g++ on PATH
echo   3^) or put Zig on PATH
exit /b 1

:built
echo.
echo [SprintSplit] Built: %OUT%
echo [SprintSplit] Next: run install.bat, or copy the dll to ..\Game\xinput1_4.dll
exit /b 0

:try_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR exit /b 1
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
echo [SprintSplit] Building with MSVC: %VSDIR%
cl /nologo /MT /O2 /std:c++17 /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS ^
   src\xinput_proxy.cpp src\config.cpp ^
   /link /DLL /DEF:src\proxy.def /OUT:"%OUT%"
exit /b 0

:try_mingw
where g++ >nul 2>nul
if errorlevel 1 exit /b 1
echo [SprintSplit] Building with MinGW g++
g++ -O2 -std=c++17 -shared -static -static-libgcc -static-libstdc++ ^
    src\xinput_proxy.cpp src\config.cpp src\proxy.def -o "%OUT%"
exit /b 0

:try_zig
where zig >nul 2>nul
if errorlevel 1 exit /b 1
echo [SprintSplit] Building with zig c++
zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -shared -static ^
    src\xinput_proxy.cpp src\config.cpp src\proxy.def -o "%OUT%"
exit /b 0

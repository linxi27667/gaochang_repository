@echo off
setlocal

REM ==========================================
REM Keil5 build script for GC-LiangZhu
REM Usage: build_keil.bat
REM Output: MDK-ARM\logs\keil_build.log
REM ==========================================

set UV4="C:\Keil_v5\UV4\UV4.exe"
set PROJ_PATH=%~dp0MDK-ARM\gaochang_lift_2.uvprojx
set LOG_DIR=%~dp0MDK-ARM\logs
set LOG_FILE=%LOG_DIR%\keil_build.log

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

echo.
echo ==============================================
echo [build] Keil5 project: %PROJ_PATH%
echo [build] Log file: %LOG_FILE%
echo ==============================================

REM Clean + Build
%UV4% -r -j0 -o "%LOG_FILE%" "%PROJ_PATH%"

set RC=%ERRORLEVEL%
echo.
echo [build] Return code: %RC%
endlocal & exit /b %RC%

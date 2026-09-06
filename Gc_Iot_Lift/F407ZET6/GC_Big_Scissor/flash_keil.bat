@echo off
setlocal

REM ==========================================
REM Keil5 flash script for GC-DaJian
REM Usage: flash_keil.bat
REM Output: MDK-ARM\logs\keil_flash.log
REM ==========================================

set UV4="C:\Keil_v5\UV4\UV4.exe"
set PROJ_PATH=%~dp0MDK-ARM\gaochang_lift_2.uvprojx
set LOG_DIR=%~dp0MDK-ARM\logs
set FLASH_LOG=%LOG_DIR%\keil_flash.log

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

echo.
echo ==============================================
echo [flash] Keil5 project: %PROJ_PATH%
echo [flash] Log file: %FLASH_LOG%
echo ==============================================

REM Flash (Download to target)
%UV4% -f -j0 -o "%FLASH_LOG%" "%PROJ_PATH%"

set RC=%ERRORLEVEL%
echo.
echo [flash] Return code: %RC%
endlocal & exit /b %RC%

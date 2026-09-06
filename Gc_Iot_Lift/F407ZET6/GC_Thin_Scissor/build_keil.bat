@echo off
setlocal

REM ==========================================
REM Keil5 build script for GC_Thin_Scissor
REM Usage: build_keil.bat
REM Output: MDK-ARM\logs\keil_build.log
REM ==========================================

set UV4=C:\Keil_v5\UV4\UV4.exe
set FROMELF=C:\Keil_v5\ARM\ARMCC\Bin\fromelf.exe
set PROJ_PATH=%~dp0MDK-ARM\GC_Thin_Scissor.uvprojx
set LOG_DIR=%~dp0MDK-ARM\logs
set LOG_FILE=%LOG_DIR%\keil_build.log
set OUT_DIR=%~dp0MDK-ARM\GC_Thin_Scissor
set AXF_PATH=%~dp0MDK-ARM\GC_Thin_Scissor\GC_Thin_Scissor.axf
set BIN_PATH=%~dp0MDK-ARM\GC_Thin_Scissor\GC_Thin_Scissor.bin

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo.
echo ==============================================
echo [build] Keil5 project: %PROJ_PATH%
echo [build] Log file: %LOG_FILE%
echo ==============================================

"%UV4%" -r -j0 -o "%LOG_FILE%" "%PROJ_PATH%"

set RC=%ERRORLEVEL%
if exist "%LOG_FILE%" (
    findstr /C:"0 Error(s)" "%LOG_FILE%" >nul
    if not errorlevel 1 set RC=0
)
if "%RC%"=="0" if exist "%AXF_PATH%" (
    "%FROMELF%" --bin --output "%BIN_PATH%" "%AXF_PATH%" >> "%LOG_FILE%" 2>&1
    if errorlevel 1 set RC=1
)
echo.
echo [build] Return code: %RC%
endlocal & exit /b %RC%

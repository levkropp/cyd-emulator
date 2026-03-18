@echo off
echo =============================================
echo CYD Emulator - GUI Mode
echo =============================================
echo.
cd /d "%~dp0build\Release"

set FIRMWARE=..\..\test-firmware\50-lvgl-basic\build\lvgl-basic-test.bin
set ELF=..\..\test-firmware\50-lvgl-basic\build\lvgl-basic-test.elf

if exist "%FIRMWARE%" (
    echo Starting with LVGL test firmware...
    echo.
    cyd-emulator.exe --firmware "%FIRMWARE%" --elf "%ELF%"
) else (
    echo No firmware specified. Showing usage...
    echo.
    cyd-emulator.exe
)

echo.
if errorlevel 1 (
    echo Emulator exited with error.
    pause
)

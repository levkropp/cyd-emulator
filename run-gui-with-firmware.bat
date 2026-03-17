@echo off
echo Starting CYD Emulator GUI with LVGL test firmware...
echo.
cd /d "C:\Users\26200.7462\cyd-emulator\build\Release"

set FIRMWARE=..\..\test-firmware\50-lvgl-basic\build\lvgl-basic-test.bin
set ELF=..\..\test-firmware\50-lvgl-basic\build\lvgl-basic-test.elf

echo Firmware: %FIRMWARE%
echo ELF: %ELF%
echo.
echo Press Ctrl+C to exit the emulator
echo.

cyd-emulator.exe --firmware "%FIRMWARE%" --elf "%ELF%"

echo.
echo Emulator closed.
pause

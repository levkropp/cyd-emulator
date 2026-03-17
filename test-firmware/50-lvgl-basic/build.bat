@echo off
REM Build script for LVGL test firmware

cd C:\Users\26200.7462\esp\esp-idf
call .venv\Scripts\activate.bat

set IDF_PATH=C:\Users\26200.7462\esp\esp-idf

cd C:\Users\26200.7462\cyd-emulator\test-firmware\50-lvgl-basic

REM Configure for ESP32
python %IDF_PATH%\tools\idf.py set-target esp32

REM Build
python %IDF_PATH%\tools\idf.py build

echo.
echo Build complete!
echo Binary: build\lvgl-basic-test.bin
echo ELF: build\lvgl-basic-test.elf

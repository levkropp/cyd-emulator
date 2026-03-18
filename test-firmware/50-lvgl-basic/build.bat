@echo off
REM Build script for LVGL test firmware
REM Requires ESP-IDF to be installed and IDF_PATH set

if not defined IDF_PATH (
    echo ERROR: IDF_PATH is not set. Please run ESP-IDF export.bat first.
    exit /b 1
)

cd /d "%~dp0"

REM Configure for ESP32
python "%IDF_PATH%\tools\idf.py" set-target esp32

REM Build
python "%IDF_PATH%\tools\idf.py" build

echo.
echo Build complete!
echo Binary: build\lvgl-basic-test.bin
echo ELF: build\lvgl-basic-test.elf

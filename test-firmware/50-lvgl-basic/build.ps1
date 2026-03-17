# Build script for LVGL test firmware

# Set IDF_PATH and remove MSYSTEM if present
$env:IDF_PATH = "C:\Users\26200.7462\esp\esp-idf"
if (Test-Path env:MSYSTEM) {
    Remove-Item env:MSYSTEM
    Write-Host "Removed MSYSTEM environment variable"
}

# Use Python 3.11 environment (created with uv)
Write-Host "Activating Python 3.11 venv..."
& C:\Users\26200.7462\esp\esp-idf\.venv\Scripts\activate.ps1

# Tell ESP-IDF to use our venv
$env:IDF_PYTHON_ENV_PATH = "C:\Users\26200.7462\esp\esp-idf\.venv"

# Add ESP32 tools to PATH
$env:PATH = "C:\Users\26200.7462\.espressif\tools\xtensa-esp32-elf\esp-12.2.0_20230208\xtensa-esp32-elf\bin;" + $env:PATH
$env:PATH = "C:\Users\26200.7462\.espressif\tools\cmake\3.30.2\bin;" + $env:PATH
$env:PATH = "C:\Users\26200.7462\.espressif\tools\ninja\1.12.1;" + $env:PATH
$env:PATH = "C:\Users\26200.7462\.espressif\tools\ccache\4.10.2\ccache-4.10.2-windows-x86_64;" + $env:PATH

# Navigate to project
Set-Location C:\Users\26200.7462\cyd-emulator\test-firmware\50-lvgl-basic

# Configure for ESP32
Write-Host "Setting target to ESP32..."
python $env:IDF_PATH\tools\idf.py set-target esp32

# Build
Write-Host "Building firmware..."
python $env:IDF_PATH\tools\idf.py build

Write-Host ""
Write-Host "Build complete!"
Write-Host "Binary: build\lvgl-basic-test.bin"
Write-Host "ELF: build\lvgl-basic-test.elf"

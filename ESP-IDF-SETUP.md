# ESP-IDF Setup for CYD Emulator

Complete guide for building ESP32 firmware for testing in cyd-emulator.

## Installation

✅ **Already Complete** - ESP-IDF v5.1.5 is installed at `C:\Users\26200.7462\esp\esp-idf`

### What Was Installed

- **Python 3.11.14** via `uv` (fast Python package manager)
- **ESP-IDF v5.1.5** (ESP32 development framework)
- **ESP32 Toolchain** (xtensa-esp32-elf compiler)
- **Python Dependencies** (52 packages including windows-curses, esptool, LVGL support)

## Building Firmware

### Quick Start (PowerShell)

```powershell
# Navigate to project
cd C:\Users\26200.7462\cyd-emulator\test-firmware\50-lvgl-basic

# Activate ESP-IDF environment
C:\Users\26200.7462\esp\esp-idf\.venv\Scripts\activate.ps1
$env:IDF_PATH="C:\Users\26200.7462\esp\esp-idf"

# Build
python $env:IDF_PATH\tools\idf.py set-target esp32
python $env:IDF_PATH\tools\idf.py build
```

### Output Files

- **Binary**: `build/lvgl-basic-test.bin`
- **ELF with symbols**: `build/lvgl-basic-test.elf`

## Running in Emulator

### Standalone Flexe Emulator

```bash
flexe/build/Release/xtensa-emu.exe -q -s build/lvgl-basic-test.elf build/lvgl-basic-test.bin
```

### GUI Emulator

```bash
build/Release/cyd-emulator.exe build/lvgl-basic-test.bin
```

## Troubleshooting

### Issue: MSYS/MinGW Warning

**Symptom**: `idf.py` warns about MSYS/MinGW not being supported

**Solution**: This is expected when running from Git Bash. The warning can be ignored as we're using a proper Python 3.11 venv. Alternatively, use PowerShell or CMD instead.

**What we did**: Patched `tools/idf_tools.py` to allow operation in MSYS environments when using venv.

### Issue: windows-curses Not Found

**Symptom**: Python package installation fails with windows-curses errors

**Solution**: We used Python 3.11 instead of 3.14. The `uv` package manager successfully installs `windows-curses==2.4.1`.

## Python Environment Details

### Using `uv` Package Manager

All Python dependencies are managed with `uv` for speed and reliability:

```bash
# Install Python 3.11
uv python install 3.11

# Create venv
uv venv --python 3.11 .venv

# Install ESP-IDF requirements (1.07s with uv!)
uv pip install -r tools/requirements/requirements.core.txt
```

### Virtual Environment Location

- **Path**: `C:\Users\26200.7462\esp\esp-idf\.venv`
- **Python**: 3.11.14 (cpython-3.11.14-windows-x86_64-none)
- **Packages**: 52 installed

## Available Test Firmware

### 50-lvgl-basic (LVGL 8.3 Test)

- **Location**: `test-firmware/50-lvgl-basic/`
- **Purpose**: Test LVGL flush callback hooks
- **Features**:
  - LVGL 8.3.x initialization
  - Custom flush callback (auto-detected by emulator)
  - Simple UI (title, subtitle, button)
  - 320×240 display (CYD screen size)

See `test-firmware/50-lvgl-basic/README.md` for details.

## Next Steps

1. **Build LVGL firmware** using PowerShell commands above
2. **Run in emulator** to test LVGL flush callback hooks
3. **Validate rendering** - verify UI appears correctly
4. **Create more test firmware** for different scenarios

## Related Documentation

- ESP-IDF Getting Started: https://docs.espressif.com/projects/esp-idf/en/v5.1.5/
- LVGL Documentation: https://docs.lvgl.io/8.3/
- uv Package Manager: https://github.com/astral-sh/uv
- Task #24: Create LVGL test firmware suite ✅
- Task #25: LVGL validation & regression testing

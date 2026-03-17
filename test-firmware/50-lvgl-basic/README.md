# LVGL Basic Test Firmware

Simple LVGL 8.x test firmware for validating LVGL support in cyd-emulator.

## Features

- LVGL 8.3.x display rendering
- Custom flush callback (`my_disp_flush`)
- Simple UI with:
  - Title label (Montserrat 24pt font)
  - Subtitle (multi-line text)
  - Button widget
- 320×240 display resolution (CYD screen)
- Double-buffered rendering (40-line buffers)

## Building

### Prerequisites

- ESP-IDF v5.1+ installed
- Python 3.11 with uv
- ESP32 target configured

### Build Steps

```bash
# Activate ESP-IDF environment
cd C:\Users\26200.7462\esp\esp-idf
.venv\Scripts\activate

# Set IDF_PATH
$env:IDF_PATH="C:\Users\26200.7462\esp\esp-idf"

# Build project
cd C:\Users\26200.7462\cyd-emulator\test-firmware\50-lvgl-basic
idf.py set-target esp32
idf.py build
```

### Output

- Binary: `build/lvgl-basic-test.bin`
- ELF with symbols: `build/lvgl-basic-test.elf`

## Running in Emulator

```bash
# Standalone flexe emulator
flexe/build/Release/xtensa-emu.exe -q -s build/lvgl-basic-test.elf build/lvgl-basic-test.bin

# GUI emulator
build/Release/cyd-emulator.exe build/lvgl-basic-test.bin
```

## Expected Behavior

The firmware should:
1. Initialize LVGL 8.x
2. Register flush callback `my_disp_flush`
3. Create UI elements (title, subtitle, button)
4. Call `lv_task_handler()` in loop
5. Trigger flush callbacks for each screen region update

The emulator's LVGL hooks should:
1. Intercept `my_disp_flush` via pattern matching
2. Read `lv_area_t` bounds from parameters
3. Copy RGB565 pixel data to emulator framebuffer
4. Call `lv_disp_flush_ready` to signal completion

## Validation

- Visual inspection: UI should render correctly in emulator window
- Log output: Flush callbacks logged with area coordinates
- Screenshot export: BMP export should match expected rendering
- Pixel readback: `readPixel()` should return correct RGB565 values

## Code Structure

- `main.c`: Application entry point and LVGL initialization
- `my_disp_flush()`: Display flush callback (intercepted by emulator)
- UI creation: Labels, button widgets
- Task loop: LVGL event handling

## Testing Flush Callback Detection

This firmware tests the emulator's ability to:
- Auto-detect flush callback via pattern `*_flush` (matches `my_disp_flush`)
- Handle LVGL 8.x API (`lv_disp_flush_ready`)
- Copy partial screen updates (lv_area_t rectangles)
- Render Montserrat fonts and LVGL widgets

## Related

- LVGL documentation: https://docs.lvgl.io/8.3/
- ESP-IDF component: https://components.espressif.com/components/lvgl/lvgl
- Task #24: Create LVGL test firmware suite
- Task #25: LVGL validation & regression testing

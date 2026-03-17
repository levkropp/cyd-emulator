# LVGL Test Firmware - Testing Notes

## Build Status

✅ **Successfully Built** (March 17, 2026)
- Binary: `build/lvgl-basic-test.bin` (338KB)
- ELF with symbols: `build/lvgl-basic-test.elf` (5.1MB)
- LVGL version: 8.3.0
- ESP-IDF version: v5.1.5

## Emulator Testing Status

⚠️ **Blocked by FreeRTOS Execution Issue**

### Test Attempt

Ran firmware in standalone flexe emulator:
```bash
flexe/build/Release/xtensa-emu.exe -q -s build/lvgl-basic-test.elf build/lvgl-basic-test.bin
```

### Result

**Trap at cycle 208911**:
```
[TRAP] Invalid PC=0x3FFB2860 at cycle 208911 (core 0, prid=0xCDCD)
Final PC: 0x3FFB2860 (disp_drv$0)
Stop reason: cpu_stopped
```

### Root Cause

The firmware crashes during FreeRTOS scheduler initialization (`vTaskStartScheduler`), **before** reaching `app_main()` where LVGL initialization occurs.

This is the known issue documented in **Task #11: Debug and fix flexe emulator hangs**.

### Observations

1. **Firmware boots correctly** through ESP32 startup:
   - CPU initialization ✅
   - Heap initialization ✅
   - Application info logged ✅
   - Scheduler start attempted ✅

2. **Crash location**: `disp_drv$0` (BSS data symbol)
   - PC jumped to data section instead of code
   - Indicates function pointer corruption or FreeRTOS context switch issue
   - Same class of bug affecting Marauder/NerdMiner firmware

3. **LVGL code never reached**:
   - No "LVGL Basic Test Starting" log message
   - No LVGL version log
   - Flush callback never called

### Next Steps

**Priority**: Fix Task #11 (FreeRTOS emulator bug) before continuing LVGL validation.

Once emulator runs FreeRTOS firmware correctly:
1. Re-run this test firmware
2. Verify flush callback auto-detection works
3. Validate framebuffer rendering
4. Export BMP screenshot for visual verification
5. Add to automated regression test suite

## Expected Behavior (When Working)

When the emulator is fixed, this firmware should:

1. **Initialize LVGL 8.x**
   - Call `lv_init()`
   - Set up display driver with `my_disp_flush` callback
   - Create double-buffered rendering (40-line buffers)

2. **Create UI**
   - Blue background (#003050)
   - White title label: "LVGL Test"
   - Gray subtitle: "CYD Emulator\nFlush Callback Test"
   - Button widget

3. **Trigger Flush Callbacks**
   - LVGL calls `my_disp_flush()` for each dirty rectangle
   - Emulator should auto-detect `my_disp_flush` symbol
   - Hook should intercept and copy RGB565 data to framebuffer
   - Emulator should call `lv_disp_flush_ready()` to signal completion

4. **Visible Output**
   - Rendered UI visible in emulator window
   - BMP export should match expected screenshot
   - Pixel readback should return correct RGB565 values

## Firmware Details

### Components Used

- **LVGL**: 8.3.0 (from ESP Component Registry)
- **ESP-IDF**: v5.1.5
- **FreeRTOS**: Included with ESP-IDF
- **ESP32 target**: esp32 (Xtensa LX6)

### Memory Configuration

- Display resolution: 320×240 (CYD screen)
- LVGL buffers: 2 × (320 × 40 × 2 bytes) = 51.2KB
- Color depth: 16-bit RGB565

### Key Functions

- `my_disp_flush()`: LVGL→Hardware display flush callback (lines 20-34)
- `app_main()`: Application entry point (lines 36-95)

### Symbols for Auto-Detection

The emulator's flush callback auto-detection should find:
- `my_disp_flush` (matches pattern `*_flush`)
- `lv_disp_flush_ready` (LVGL 7.x/8.x compatibility)

## References

- Main code: `main/main.c`
- Build script: `build.ps1`
- Component manifest: `main/idf_component.yml`
- Configuration: `sdkconfig.defaults`
- README: `README.md`
- Task #25: LVGL validation and regression testing
- Task #11: Debug and fix flexe emulator hangs (BLOCKER)

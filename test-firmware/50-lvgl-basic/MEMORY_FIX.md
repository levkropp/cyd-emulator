# LVGL Memory Configuration Fix

## Problem

The LVGL test firmware gets stuck in an infinite loop at `lv_obj_allocate_spec_attr+0x1C` during initialization due to memory exhaustion.

## Root Cause

**Disassembly of stuck location (0x400D64FC):**
```asm
0x400D64F5: call8   0x400df49c   # Call lv_mem_alloc
0x400D64F8: s32i.n  a10, a2, 8   # Store result to obj->spec_attr
0x400D64FA: bnez.n  a10, 0x400d6501  # If allocation succeeded, continue
0x400D64FC: j       0x400d64fc   # INFINITE LOOP - deliberate panic on NULL
```

This is **intentional firmware behavior** - when `lv_mem_alloc()` returns NULL (out of memory), the code enters a deliberate infinite loop as a panic/halt mechanism.

## Why 32 KB Was Too Small

**Original Configuration:** `CONFIG_LV_MEM_SIZE_KILOBYTES=32` (the ESP-IDF default)

**Recommended by LVGL:** 48 KB for applications using several objects

According to [LVGL documentation](https://docs.lvgl.io/8.3/intro/index.html) and [ESP32 examples](https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/examples/platformio/lvgl_v8_port/src/lv_conf.h):
> The dynamic data (heap) requirement for LVGL is > 4 KB (> 48 kB is recommended if using several objects)

The test firmware creates:
- 1 screen object with background styling
- 2 labels (title + subtitle with multi-line text)
- 1 button (120x50 px)
- 1 button label

Even this minimal UI exhausts 32 KB during LVGL's object allocation, style management, and event system initialization.

## Solution

**Update `sdkconfig`:**
```diff
-CONFIG_LV_MEM_SIZE_KILOBYTES=32
+CONFIG_LV_MEM_SIZE_KILOBYTES=48
```

**Rebuild:**
```bash
# Must do FULL clean build to regenerate config
cd test-firmware/50-lvgl-basic
rm -rf build
powershell.exe -ExecutionPolicy Bypass -File build.ps1
```

**Note:** Simply deleting `build/config` is NOT sufficient - the CMake cache must be fully regenerated.

## ESP32 CYD Hardware Compatibility

The ESP32-2432S028R (Cheap Yellow Display) has:
- 520 KB internal SRAM
- 4-8 MB PSRAM (optional, model dependent)

Allocating 48 KB for LVGL heap (9% of internal SRAM) is reasonable and matches configurations used by real CYD projects in the wild.

## References

- [LVGL ESP32 CYD Tutorial - Random Nerd Tutorials](https://randomnerdtutorials.com/lvgl-cheap-yellow-display-esp32-2432s028r/)
- [LVGL Documentation - Memory Configuration](https://docs.lvgl.io/8.3/intro/index.html)
- [ESP32 Display Panel LVGL Config Example](https://github.com/esp-arduino-libs/ESP32_Display_Panel/blob/master/examples/platformio/lvgl_v8_port/src/lv_conf.h)

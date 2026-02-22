# cyd-emulator

Desktop emulator for ESP32 CYD (Cheap Yellow Display) firmware. Runs your ESP32 app code natively on Linux using SDL2, with an emulated 320x240 ILI9341 display, resistive touchscreen, and SD card.

## What it does

- Compiles your ESP32 firmware's C source files directly (no cross-compilation)
- Renders the ILI9341 display to an SDL2 window at 60 FPS
- Emulates touch input via mouse clicks
- Attaches FAT32 or exFAT SD card images via File menu
- Provides ESP-IDF shim headers so `#include "esp_log.h"` etc. just work
- Info panel shows chip model, display resolution, touch events, and SD card status
- Supports multiple CYD board profiles (`--board 2432S028R`, `--board 8048S070`, etc.)

## Screenshot

```
+--[ CYD Emulator ]--+----------+
| File  View  Help   |          |
+--------------------+  Info    |
|                    |  Panel   |
|   320 x 240       |          |
|   Emulated LCD     |          |
|                    |          |
+--------------------+----------+
```

## Building

### Dependencies

- SDL2 development libraries
- CMake 3.10+
- miniz (`libminiz-dev` or equivalent)
- The ESP32 firmware source (defaults to `../survival/esp32/main`)

### Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

To point at a different app source directory:

```bash
cmake -DAPP_SOURCE_DIR=/path/to/your/esp32/main ..
```

### Run

```bash
./cyd-emulator                           # default board
./cyd-emulator --board 8048S070          # 800x480 S3 board
./cyd-emulator --sd test/sdcard-fat32.img  # pre-attach SD card
```

## Architecture

```
src/
  emu_main.c      Main thread: SDL window, event loop, menu bar, info panel
  emu_display.c   Framebuffer: RGB565 pixel ops, shared via mutex
  emu_touch.c     Touch emulation: mouse events -> touch_read() API
  emu_sdcard.c    SD card: attach/detach disk images, block-level I/O
  emu_payload.c   Payload stub: emulates firmware update flash region
  emu_crc32.c     CRC32 shim: wraps miniz crc32
  emu_json.c      Minimal JSON parser for board profile configs

include/          ESP-IDF shim headers (esp_log.h, freertos/task.h, etc.)
```

The emulator launches two threads:
1. **Main thread** -- SDL init, window creation, event loop, menu rendering
2. **App thread** -- calls `app_main()` from your firmware, exactly as ESP-IDF would

The display framebuffer and touch state are shared between threads via mutexes.

## How it works with your firmware

Your firmware calls functions like `display_fill_rect()`, `touch_read()`, `sdcard_read_sector()`. On the real ESP32, these talk to SPI peripherals. In the emulator, shim implementations in `src/emu_*.c` redirect them to SDL2 and file I/O. Your app code is compiled verbatim -- no `#ifdef EMULATOR` needed.

## License

MIT

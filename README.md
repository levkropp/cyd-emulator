# cyd-emulator

![screenshot](screenshot.png)

Desktop emulator for ESP32 CYD (Cheap Yellow Display) firmware. Interprets genuine ESP32 .bin firmware binaries via the built-in **flexe** Xtensa LX6 emulator, with an emulated 320x240 ILI9341 display, resistive touchscreen, and SD card.

## Quick start

```bash
git clone https://github.com/nocomp/cyd-emulator.git
cd cyd-emulator
cmake -S . -B build && cmake --build build
./build/cyd-emulator --firmware /path/to/firmware.bin --elf /path/to/firmware.elf
```

The `--firmware` flag is required. The `--elf` flag is optional but enables symbol-based function hooking (ROM stubs, FreeRTOS, display/touch/SD drivers).

## What it does

- Interprets ESP32 firmware binaries through a cycle-level Xtensa LX6 emulator
- Renders the ILI9341 display to an SDL2 window at 60 FPS
- Emulates touch input via mouse clicks
- Attaches FAT32 or exFAT SD card images via File menu or `--sdcard`
- Hooks ESP-IDF runtime functions: FreeRTOS, NVS, esp_timer, GPIO, heap, logging
- ROM function stubs: ets_printf, memcpy, memset, strlen, Cache ops, delay
- Display/touch/SD card stubs render into the emulator window
- Info panel shows chip model, display resolution, touch events, and UART log
- Supports multiple CYD board profiles (`--board 2432S028R`, `--board 8048S070`, etc.)
- Scriptable control interface via Unix domain socket (`--control`)

## Dependencies

- SDL2 development libraries (`libsdl2-dev` or equivalent)
- CMake 3.14+
- C compiler (GCC or Clang)

## Usage

```bash
./build/cyd-emulator --firmware app.bin                     # minimal
./build/cyd-emulator --firmware app.bin --elf app.elf       # with symbol hooking
./build/cyd-emulator --firmware app.bin --sdcard sd.img     # pre-attach SD card
./build/cyd-emulator --firmware app.bin --board 8048S070    # 800x480 S3 board
./build/cyd-emulator --firmware app.bin --turbo             # fast SD I/O
./build/cyd-emulator --firmware app.bin --control /tmp/ctl  # scripted control
```

### Options

| Flag | Description |
|------|-------------|
| `--firmware <file>` | ESP32 firmware binary (required) |
| `--elf <file>` | ELF file for symbol hooking |
| `--board <model>` | Board profile (default: 2432S028R) |
| `--sdcard <file>` | SD card image path (default: sd.img) |
| `--sdcard-size <size>` | SD card size, e.g. 4G |
| `--scale <1-4>` | Display scale factor (default: 2) |
| `--turbo` | Start in turbo mode |
| `--control <path>` | Unix socket for scripted control |

### Controls

| Key | Action |
|-----|--------|
| Click on display | Tap touchscreen |
| Tab | Toggle turbo mode |
| R | Restart app |
| Q / Ctrl+C | Quit |

### Control socket

When started with `--control <path>`, the emulator listens for single-line commands:

```bash
echo "tap 160 120" | socat - UNIX:/tmp/ctl    # tap center of screen
echo "screenshot /tmp/shot.bmp" | socat - UNIX:/tmp/ctl
echo "status" | socat - UNIX:/tmp/ctl
echo "pause" | socat - UNIX:/tmp/ctl           # debug: pause CPU
echo "regs" | socat - UNIX:/tmp/ctl            # debug: dump registers
echo "continue" | socat - UNIX:/tmp/ctl        # debug: resume
```

## Architecture

```
src/
  emu_main.c      Main thread: SDL window, event loop, menu bar, info panel
  emu_display.c   Framebuffer: RGB565 pixel ops, shared via mutex
  emu_touch.c     Touch emulation: mouse events -> touch_read() API
  emu_sdcard.c    SD card: attach/detach disk images, block-level I/O
  emu_flexe.c     Bridge to flexe Xtensa interpreter
  emu_crc32.c     CRC32 utility
  emu_json.c      Save/load emulator state (JSON + SD image)
  emu_freertos.c  FreeRTOS emulation: tasks, semaphores, queues, timers
  emu_control.c   Unix socket control interface + debug commands
  font.c          Bitmap font data for panel rendering

flexe/            Xtensa LX6 emulator (subproject)
  src/xtensa.c    CPU core: decode, execute, windowed registers
  src/memory.c    Memory subsystem: regions, IRAM/DRAM/flash
  src/loader.c    ESP32 firmware binary loader
  src/peripherals.c  ESP32 MMIO: UART, GPIO, DPORT, timers, etc.
  src/rom_stubs.c    ROM function hooking via PC traps
  src/elf_symbols.c  ELF symbol table loading
  src/freertos_stubs.c  FreeRTOS runtime hooks
  src/display_stubs.c   SPI display driver hooks
  src/touch_stubs.c     Touch controller hooks
  src/sdcard_stubs.c    SD/MMC driver hooks

include/          ESP-IDF shim headers + shared API headers
```

The emulator launches two threads:
1. **Main thread** -- SDL init, window creation, event loop, menu rendering
2. **App thread** -- runs firmware via Xtensa interpreter

The display framebuffer and touch state are shared between threads via mutexes.

## License

MIT

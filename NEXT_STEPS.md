# Next Steps - CYD Emulator Development

## ✅ Completed

1. **Native Windows Build** (Tasks #1, #2)
   - Full MSVC compilation support
   - All 459 flexe unit tests passing
   - Built executables: xtensa-emu.exe, xtensa-tests.exe, xt-dis.exe, trace-filter.exe, cyd-emulator.exe
   - Comprehensive POSIX compatibility layer (msvc_compat.h/c)
   - vcpkg integration for dependencies

2. **Test Suite Architecture** (Task #4)
   - Designed progressive test levels (0-4)
   - Created test-firmware/README.md with complete plan
   - Defined 40+ systematic tests for validation

## 🚧 Current Blockers

### Missing ESP32 Firmware
**Problem**: No test firmware binaries (.bin + .elf) available to run the emulator.

**Options to Resolve**:

1. **Install ESP-IDF on Windows** (Recommended for long-term)
   ```powershell
   # Download ESP-IDF installer from https://dl.espressif.com/dl/esp-idf/
   # Install to C:\esp\esp-idf
   # Add to PATH
   idf.py --version
   ```

2. **Use WSL2 + ESP-IDF** (Easier setup)
   ```bash
   wsl --install
   # Inside WSL:
   sudo apt-get install git wget flex bison gperf python3
   git clone -b v5.1 --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32
   . ./export.sh
   ```

3. **Cross-compile on Linux** (If available)
   - Build firmware on Linux machine
   - Copy .bin + .elf to Windows

4. **Download Pre-built Firmware** (Quick test)
   - Find Marauder/NerdMiner releases
   - Download .bin files
   - May not have .elf symbols

## 📋 Immediate Next Tasks

### LVGL Support (Priority)

**Task #21: Implement LVGL 8.x/9.x Flush Callback Hooks**
- ✅ COMPLETED - Added PC hooks for `lv_display_flush_ready` and common flush callbacks
- ✅ Implemented flush handler in `display_stubs.c`
- ✅ Handles partial update regions (lv_area_t)
- ✅ Added mutex protection for framebuffer writes
- ✅ All 459 unit tests still pass

**Task #22: Implement LVGL 7.x Compatibility Layer**
- Add PC hooks for LVGL 7.x API (`lv_disp_flush_ready`)
- Version detection logic
- Backward compatibility testing

**Task #23: ELF Symbol Auto-Detection for Flush Callbacks**
- Scan ELF symbols for common flush callback patterns
- Auto-register PC hooks for detected symbols
- Implement fallback to TFT_eSPI interception

**Task #24: Create LVGL Test Firmware Suite**
- Build test-firmware/50-lvgl-basic (LVGL 7.x, 8.x, 9.x)
- Simple UI with buttons, labels, sliders
- Touch interaction validation

**Task #25: LVGL Validation & Regression Testing**
- Automated BMP screenshot comparison
- Pixel readback verification
- Integration with existing test framework

**Task #26: Test Real-World LVGL Applications**
- Marauder with LVGL UI elements
- Community LVGL demos/projects
- Document known limitations

### Without Firmware (Code Analysis)

**Task #5: Enhanced Tracing Tools**
- ✅ trace-filter.exe already built
- Add more filter modes for debugging
- Create trace analysis scripts

**Task #10: Debug Font Rendering Issues**
- ✅ COMPLETED - Fixed Font 2 bytes-per-row calculation bug
- ✅ Created comprehensive FONT_RENDERING_ANALYSIS.md

**Task #11: Debug Emulator Hangs**
- **Approach**:
  1. Review freertos_stubs.c task scheduler
  2. Check for infinite loops in task switching
  3. Add debug logging to scheduler
  4. Review timer callback handling

### With Firmware (Testing & Validation)

**Priority Order**:

1. **Get ANY firmware running**
   - Try to find pre-built .bin online
   - Test basic emulator functionality
   - Verify Windows build works end-to-end

2. **Build Simple Test Firmware** (test-firmware/01-hello-world/)
   - Minimal ESP32 project
   - Print to UART only
   - Verify flexe executes correctly

3. **Build Display Test** (test-firmware/02-display-init/)
   - Initialize TFT_eSPI
   - Fill screen with solid colors
   - Verify framebuffer works

4. **Build Font Test** (test-firmware/11-gfx-basic/)
   - Render text with GFXfont
   - Reproduce font rendering bugs
   - Debug with xtensa-emu -T traces

5. **Progressive Testing**
   - Work through all Level 0-4 tests
   - Identify and fix bugs systematically
   - Create regression test suite

## 🔧 Tools Available

### Debugging Commands

```bash
# Run with verbose trace
flexe/build/Release/xtensa-emu.exe -q -T -s firmware.elf firmware.bin 2>trace.log

# Filter trace for specific issues
flexe/build/Release/trace-filter.exe -u trace.log  # Unregistered ROM calls
flexe/build/Release/trace-filter.exe -e trace.log  # Exceptions
flexe/build/Release/trace-filter.exe -p trace.log  # Panic/abort path

# Run with event log
flexe/build/Release/xtensa-emu.exe -E -P 1000000 -q -s firmware.elf firmware.bin

# GUI emulator
build/Release/cyd-emulator.exe firmware.bin
```

### Code Analysis Tools

```bash
# Find font rendering code
grep -rn "GFXfont" flexe/src/
grep -rn "render.*glyph" flexe/src/

# Find task scheduler code
grep -rn "task.*switch\|scheduler" flexe/src/freertos_stubs.c

# Find timer handling
grep -rn "timer.*callback" flexe/src/
```

## 📊 Known Issues to Debug

From user description:

1. **Font Rendering Bugs**
   - Symptoms: Artifacts, incorrect glyphs, spacing issues
   - Location: flexe/src/display_stubs.c:148-191 (render_gfxfont_glyph)
   - Suspects:
     - Bit packing/unpacking errors
     - xoff/yoff coordinate calculation
     - Scaling logic for different font sizes
     - Bounds checking (lines 178, 181)

2. **Emulator Hangs**
   - Symptoms: Infinite loops, frozen execution
   - Location: flexe/src/freertos_stubs.c (task scheduler)
   - Suspects:
     - Deadlock in task switching
     - Timer overflow issues
     - Infinite loop in waiting conditions
     - Priority inversion

3. **Stack Failures**
   - Symptoms: Crashes, memory corruption
   - Location: Various stub modules
   - Suspects:
     - Stack overflow in FreeRTOS tasks
     - Memory allocation issues
     - Register window management bugs

## 🎯 Recommended Approach

### Option A: Pure Code Analysis (No Firmware)
1. ✅ Read and analyze display_stubs.c GFXfont code
2. Create standalone unit test for glyph rendering
3. Add comprehensive bounds checking
4. Review scheduler code for deadlocks
5. Add assertion checks and debug logging

### Option B: Get Firmware First (Recommended)
1. Install ESP-IDF or use WSL2
2. Build test-firmware/01-hello-world
3. Run in xtensa-emu.exe with -T trace
4. Verify basic execution works
5. Build progressively complex tests
6. Reproduce bugs systematically
7. Fix issues with trace-based debugging

### Option C: Hybrid Approach
1. Start code analysis now (Option A steps 1-2)
2. Set up ESP-IDF in parallel
3. Once firmware available, switch to testing
4. Iterate between code fixes and testing

## 📝 Documentation Needs

- [ ] Task #3: Testing framework documentation
- [ ] Task #15: Comprehensive documentation
- [ ] User guide for building test firmware
- [ ] Debugging guide with trace-filter examples
- [ ] Known issues and workarounds

## 🔄 Continuous Integration (Task #14)

- GitHub Actions workflow for Windows build
- Automated test suite (459 tests)
- Firmware build pipeline (when ESP-IDF set up)
- Regression test framework

---

**Current Status**: ✅ Emulator builds and tests pass on Windows
**Blocker**: Need firmware binaries to run actual tests
**Next Action**: Choose Option A, B, or C above

# FreeRTOS Minimal Test Firmware

Minimal FreeRTOS test firmware for validating cyd-emulator's FreeRTOS support.

## Purpose

This is the **simplest possible FreeRTOS application** to test:
- Basic FreeRTOS scheduler functionality
- Task creation and deletion
- Task switching between multiple tasks
- vTaskDelay() timing
- Logging from tasks
- Heap management

## What It Does

1. Creates two tasks:
   - **Task 1**: Prints a message every 1 second (5 iterations)
   - **Task 2**: Prints a message every 1.5 seconds (3 iterations)

2. Both tasks run concurrently under FreeRTOS scheduler

3. Tasks self-delete after completing their iterations

## Expected Output

```
I (xxx) freertos-test: ===========================================
I (xxx) freertos-test: FreeRTOS Minimal Test Starting
I (xxx) freertos-test: ===========================================
I (xxx) freertos-test: Free heap: XXXXX bytes
I (xxx) freertos-test: Creating Task 1...
I (xxx) freertos-test: Creating Task 2...
I (xxx) freertos-test: Tasks created. Scheduler running.
I (xxx) freertos-test: Free heap: XXXXX bytes
I (xxx) freertos-test: app_main() exiting, tasks will continue
I (xxx) freertos-test: Task 1 started
I (xxx) freertos-test: Task 2 started
I (xxx) freertos-test: Task 1: count = 0
I (xxx) freertos-test: Task 2: count = 0
I (xxx) freertos-test: Task 1: count = 1
I (xxx) freertos-test: Task 2: count = 1
I (xxx) freertos-test: Task 1: count = 2
I (xxx) freertos-test: Task 1: count = 3
I (xxx) freertos-test: Task 2: count = 2
I (xxx) freertos-test: Task 1: count = 4
I (xxx) freertos-test: Task 1: Completed 5 iterations, exiting
I (xxx) freertos-test: Task 1 finished
I (xxx) freertos-test: Task 2: Completed 3 iterations, exiting
I (xxx) freertos-test: Task 2 finished
```

## Building

```powershell
cd test-firmware/10-freertos-minimal
.\build.ps1
```

Or manually:
```powershell
$env:IDF_PATH="C:\Users\26200.7462\esp\esp-idf"
C:\Users\26200.7462\esp\esp-idf\.venv\Scripts\activate.ps1
idf.py set-target esp32
idf.py build
```

## Running in Emulator

```bash
# Standalone flexe emulator
flexe/build/Release/xtensa-emu.exe -q -s build/freertos-minimal-test.elf build/freertos-minimal-test.bin

# GUI emulator
build/Release/cyd-emulator.exe build/freertos-minimal-test.bin
```

## Testing Goals

This firmware helps validate:

1. **✅ FreeRTOS scheduler starts correctly**
   - Scheduler initialization
   - Idle task creation
   - Timer task creation

2. **✅ Task creation works**
   - xTaskCreate() allocates TCB and stack
   - Tasks are added to ready list

3. **✅ Context switching works**
   - Switching between tasks
   - Register save/restore
   - Stack pointer switching

4. **✅ vTaskDelay() works**
   - Tasks block on delay
   - Tick interrupts advance time
   - Delayed tasks wake up correctly

5. **✅ Task deletion works**
   - vTaskDelete() removes task
   - Memory is freed
   - Scheduler continues with remaining tasks

6. **✅ Logging from tasks works**
   - ESP_LOGI() from different tasks
   - UART output is correct
   - No corruption or race conditions

## Success Criteria

The firmware runs successfully if:
- All log messages appear in order
- Both tasks complete their iterations
- No crashes or traps occur
- Tasks exit cleanly
- Emulator doesn't hang

## Debugging

If this firmware fails:
1. Check which task is running when crash occurs
2. Check register state (especially a1 = stack pointer)
3. Check if context switch is happening correctly
4. Look for WindowOverflow/Underflow exceptions
5. Verify tick timer is working

## Related Tasks

- Task #11: Debug and fix flexe emulator hangs (this firmware helps debug this)
- Task #25: LVGL validation (blocked until FreeRTOS works)
- Task #12: Test Marauder firmware (uses FreeRTOS)
- Task #13: Test NerdMiner firmware (uses FreeRTOS)

# FreeRTOS Comprehensive Validation Plan

## Current Status

### What We Know
- ✅ FreeRTOS scheduler runs for 10M cycles without crashing
- ✅ Tasks are created, switched, delayed, and deleted
- ⚠️ **Flexe is single-core only** - Core 1 is stubbed (sets `s_cpu_inited[1]=1`)
- ⚠️ **10M cycles is insufficient** - Need to test for billions of cycles
- ⚠️ **No save/restore capability** - Can't debug long-running failures efficiently

### Critical Gaps

1. **Dual-core emulation**: FreeRTOS on ESP32 runs on both cores
   - Core 0 (PRO_CPU): Protocol CPU
   - Core 1 (APP_CPU): Application CPU
   - Current emulator only executes Core 0

2. **Insufficient test duration**:
   - 10M cycles ≈ 0.0625 seconds at 160MHz
   - Real firmware crashes have been seen after 5 billion cycles
   - Need 500x longer tests minimum

3. **Limited observability for long runs**:
   - Trace output becomes massive (gigabytes)
   - Can't checkpoint/restore state
   - Hard to isolate intermittent bugs
   - No way to "rewind" and inspect before crash

## Phase 1: Enhance Test Infrastructure

### 1.1 Save State / Checkpoint System

**Goal**: Save complete emulator state for debugging and reproduction

**Implementation**:
```c
// flexe/src/savestate.h
typedef struct {
    // CPU state
    xtensa_cpu_t cpu_snapshot;
    uint64_t cycle_count;

    // Memory snapshots
    uint8_t *iram_data;
    uint8_t *dram_data;
    uint8_t *flash_data;
    size_t iram_size, dram_size, flash_size;

    // FreeRTOS state
    void *freertos_state;  // Task lists, queues, etc.

    // Peripherals state
    void *peripheral_state;

    // Metadata
    uint64_t timestamp;
    char description[256];
} savestate_t;

int savestate_create(xtensa_cpu_t *cpu, const char *path);
int savestate_restore(xtensa_cpu_t *cpu, const char *path);
```

**Features**:
- Periodic auto-save every N cycles
- Save on demand via signal (SIGUSR1)
- Save on crash/exception
- Compressed format (zlib)
- Incremental/diff-based saves for efficiency

### 1.2 Long-Duration Test Harness

**Script**: `test-firmware/freertos-stress-test.sh`

```bash
#!/bin/bash
# Run FreeRTOS test for billions of cycles with checkpointing

FIRMWARE="$1"
ELF="$2"
MAX_CYCLES="${3:-5000000000}"  # 5 billion default
CHECKPOINT_INTERVAL="${4:-100000000}"  # 100M cycles
TRACE_WINDOW="${5:-1000000}"  # Only trace last 1M cycles before crash

# Progressive run with checkpoints
./flexe/build/Release/xtensa-emu \
    -q \
    -s "$ELF" \
    -c "$MAX_CYCLES" \
    -E \
    -P 50000000 \
    -T "$((MAX_CYCLES - TRACE_WINDOW)):$MAX_CYCLES" \
    -D crash \
    --checkpoint-interval "$CHECKPOINT_INTERVAL" \
    --checkpoint-dir ./checkpoints \
    "$FIRMWARE" \
    2>&1 | tee freertos-stress.log

# If crashed, analyze
if [ $? -ne 0 ]; then
    echo "CRASH DETECTED - Analyzing..."
    ./flexe/build/Release/trace-filter -A freertos-stress.log

    # Find nearest checkpoint
    LAST_CHECKPOINT=$(ls -t checkpoints/*.sav | head -1)
    echo "Nearest checkpoint: $LAST_CHECKPOINT"

    # Replay from checkpoint with full trace
    ./flexe/build/Release/xtensa-emu \
        --restore "$LAST_CHECKPOINT" \
        -T \
        -v \
        "$FIRMWARE"
fi
```

### 1.3 Event-Driven Trace System

**Problem**: Verbose trace generates gigabytes of data
**Solution**: Smart event-only logging

```c
// flexe/src/event_log.h
typedef enum {
    EVENT_TASK_CREATE,
    EVENT_TASK_DELETE,
    EVENT_TASK_SWITCH,
    EVENT_QUEUE_SEND,
    EVENT_QUEUE_RECEIVE,
    EVENT_SEMAPHORE_TAKE,
    EVENT_SEMAPHORE_GIVE,
    EVENT_TIMER_FIRED,
    EVENT_INTERRUPT,
    EVENT_EXCEPTION,
    EVENT_ROM_CALL,
    EVENT_PERIPHERAL_ACCESS,
} event_type_t;

typedef struct {
    uint64_t cycle;
    uint32_t pc;
    event_type_t type;
    uint32_t data[4];  // Event-specific data
} event_record_t;

// Compact binary log (8-16 bytes per event vs KB per instruction trace)
void event_log_record(event_type_t type, uint32_t pc, ...);
void event_log_dump(const char *path);
event_record_t* event_log_query(uint64_t start_cycle, uint64_t end_cycle);
```

**Benefits**:
- 1000x smaller than instruction trace
- Can run for billions of cycles
- Post-process to find anomalies
- Correlate events (task switch + exception = bug)

## Phase 2: Dual-Core Emulation (Future Work)

**Complexity**: High - requires threading and synchronization

**Approach**:
1. **pthread-based execution**:
   - Core 0 thread
   - Core 1 thread
   - Lock-stepped or async execution?

2. **Memory coherency**:
   - Shared memory with atomic access
   - Cache simulation (optional)

3. **Inter-core communication**:
   - FIFO interrupts
   - Crosscore semaphores

**Decision**: De-prioritize for now. Most FreeRTOS bugs manifest on single-core too.

## Phase 3: Comprehensive FreeRTOS Test Suite

### 3.1 Test Levels

**Level 1: Basic Primitives** (Already have: 10-freertos-minimal)
- Task create/delete
- vTaskDelay
- Logging

**Level 2: Synchronization**
```
test-firmware/20-freertos-sync/
- Mutexes (priority inheritance)
- Semaphores (binary, counting)
- Queues (send, receive, blocking)
- Event groups
```

**Level 3: Advanced Features**
```
test-firmware/30-freertos-advanced/
- Task priorities
- Preemption
- Tickless idle
- Memory allocation
- Software timers
```

**Level 4: Stress Tests**
```
test-firmware/40-freertos-stress/
- Many tasks (20+)
- Deep call stacks
- Rapid context switching
- Queue flooding
- Timer storms
```

**Level 5: Real-World Patterns**
```
test-firmware/45-freertos-patterns/
- Producer-consumer with queues
- State machines
- Periodic tasks
- Interrupt handlers → task notifications
```

### 3.2 Test Matrix

| Test | Duration | Expected Outcome | Pass Criteria |
|------|----------|------------------|---------------|
| 10-freertos-minimal | 100M cycles | Tasks run and exit | 5 iterations each task, no crash |
| 20-freertos-sync | 500M cycles | Synchronization works | No deadlocks, correct ordering |
| 30-freertos-advanced | 1B cycles | Timers fire, priorities respected | All timers fire, high-pri runs first |
| 40-freertos-stress | 5B cycles | No crashes under load | Stability for full 5B cycles |
| 45-freertos-patterns | 2B cycles | Real patterns work | Patterns execute correctly |

### 3.3 Automated Validation

```python
# test-firmware/validate_freertos.py
import subprocess
import re

tests = [
    ("10-freertos-minimal", 100_000_000, ["Task 1 finished", "Task 2 finished"]),
    ("20-freertos-sync", 500_000_000, ["Mutex test passed", "Queue test passed"]),
    # ...
]

def run_test(name, cycles, expected_outputs):
    result = subprocess.run([
        "./flexe/build/Release/xtensa-emu",
        "-q", "-E", "-P", "10000000",
        "-s", f"test-firmware/{name}/build/{name}.elf",
        "-c", str(cycles),
        f"test-firmware/{name}/build/{name}.bin"
    ], capture_output=True, text=True)

    # Check exit code
    if result.returncode != 0:
        return False, "Crashed"

    # Check expected outputs
    for expected in expected_outputs:
        if expected not in result.stderr:
            return False, f"Missing: {expected}"

    return True, "PASS"

# Run all tests
for test_name, cycles, outputs in tests:
    passed, reason = run_test(test_name, cycles, outputs)
    print(f"[{'PASS' if passed else 'FAIL'}] {test_name}: {reason}")
```

## Phase 4: Known Issues to Investigate

### Issue 1: LVGL Timer Callback Corruption

**Symptom**: PC jumps to data section (`disp_drv$0`)
**Location**: `lv_timer_exec` trying to call `timer->timer_cb`
**Hypothesis**: LVGL timer structure not initialized, or corrupted by memory issue

**Debug Plan**:
1. Add checkpoint before LVGL init
2. Trace all writes to LVGL timer structures
3. Validate timer->timer_cb pointer before each call
4. Check for buffer overruns in FreeRTOS heap

### Issue 2: Unreported UART Output

**Symptom**: `esp_log_write` called 28x but only 1279 bytes captured
**Expected**: ~28 log messages ≈ 1-2KB
**Hypothesis**: UART stub may be dropping data or buffering incorrectly

**Debug Plan**:
1. Instrument UART stub to count actual writes
2. Check for buffer overflows
3. Verify newline handling

## Implementation Priority

### Immediate (This Week)
1. ✅ Create minimal FreeRTOS test (done)
2. Add save/restore capability
3. Run minimal test for 5B cycles with checkpoints
4. Fix UART output capture

### Short Term (Next Week)
5. Create Level 2-3 FreeRTOS tests
6. Implement event log system
7. Run full test matrix
8. Document all findings

### Medium Term (2-4 Weeks)
9. Create Level 4-5 stress tests
10. Automated regression suite
11. Investigate and fix LVGL crash
12. Return to LVGL validation with confidence

### Long Term (Future)
13. Dual-core emulation (if needed)
14. Coverage-guided fuzzing
15. Formal verification of critical paths

## Success Criteria

FreeRTOS validation is complete when:

1. ✅ All test levels (1-5) pass for full duration
2. ✅ No crashes or hangs for 5B+ cycles
3. ✅ Save/restore works reliably
4. ✅ Event log captures all relevant state transitions
5. ✅ UART output is 100% captured
6. ✅ Automated regression suite passes
7. ✅ Documentation complete

Only then do we return to LVGL testing.

## Resources

- Existing trace tools: `-T`, `-E`, `-P`, `-W`, `-F`, `trace-filter`
- Test framework: 459 unit tests in `flexe/tests/`
- ESP-IDF: v5.1.5 with FreeRTOS 10.5.1
- Hardware reference: ESP32 (Xtensa LX6, dual-core, 160/240MHz)

---

**Status**: Planning Phase
**Owner**: Task #11 (Debug and fix flexe emulator hangs)
**Blocking**: Task #25 (LVGL validation), #12 (Marauder), #13 (NerdMiner)

#!/bin/bash
# Long-duration FreeRTOS stress test with checkpointing
# Usage: ./freertos-stress-test.sh <firmware.bin> <firmware.elf> [max_cycles] [checkpoint_interval]

set -e

FIRMWARE="$1"
ELF="$2"
MAX_CYCLES="${3:-5000000000}"         # 5 billion default (31 seconds @ 160MHz)
CHECKPOINT_INTERVAL="${4:-100000000}" # 100M cycles (0.625 sec checkpoints)
TRACE_WINDOW="${5:-10000000}"         # Only trace last 10M cycles before crash

if [ -z "$FIRMWARE" ] || [ -z "$ELF" ]; then
    echo "Usage: $0 <firmware.bin> <firmware.elf> [max_cycles] [checkpoint_interval]"
    exit 1
fi

if [ ! -f "$FIRMWARE" ]; then
    echo "Error: Firmware file not found: $FIRMWARE"
    exit 1
fi

if [ ! -f "$ELF" ]; then
    echo "Error: ELF file not found: $ELF"
    exit 1
fi

CHECKPOINT_DIR="./checkpoints"
LOG_FILE="freertos-stress-$(date +%s).log"
EMULATOR="../flexe/build/Release/xtensa-emu"

if [ ! -f "$EMULATOR" ]; then
    EMULATOR="../flexe/build/xtensa-emu"
fi

if [ ! -f "$EMULATOR" ]; then
    echo "Error: Emulator not found. Please build flexe first:"
    echo "  cd ../flexe && cmake --build build --config Release"
    exit 1
fi

mkdir -p "$CHECKPOINT_DIR"

echo "=== FreeRTOS Long-Duration Stress Test ==="
echo "Firmware:     $FIRMWARE"
echo "ELF:          $ELF"
echo "Max cycles:   $MAX_CYCLES ($(echo "scale=2; $MAX_CYCLES / 160000000" | bc 2>/dev/null || echo "?") sec @ 160MHz)"
echo "Checkpoints:  Every $CHECKPOINT_INTERVAL cycles"
echo "Checkpoint dir: $CHECKPOINT_DIR"
echo "Trace window: Last $TRACE_WINDOW cycles only (on crash)"
echo "Log:          $LOG_FILE"
echo

# Calculate trace window start (only trace near the end)
TRACE_START=$((MAX_CYCLES - TRACE_WINDOW))
if [ $TRACE_START -lt 0 ]; then
    TRACE_START=0
fi

# Run with periodic checkpointing
# -q: quiet (suppress peripheral warnings)
# -E: event log (task switches, ROM calls)
# -P: progress heartbeat every 50M cycles
# -T START:END: verbose trace only in final window (to reduce log size)
# --checkpoint-interval: auto-save every N cycles
# --checkpoint-dir: where to save checkpoints

echo "Starting emulation..."
echo "Command: $EMULATOR -q -s \"$ELF\" -c $MAX_CYCLES -E -P 50000000 --checkpoint-interval $CHECKPOINT_INTERVAL --checkpoint-dir \"$CHECKPOINT_DIR\" \"$FIRMWARE\""
echo

"$EMULATOR" \
    -q \
    -s "$ELF" \
    -c "$MAX_CYCLES" \
    -E \
    -P 50000000 \
    --checkpoint-interval "$CHECKPOINT_INTERVAL" \
    --checkpoint-dir "$CHECKPOINT_DIR" \
    "$FIRMWARE" \
    2>&1 | tee "$LOG_FILE"

EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo
    echo "!!! CRASH DETECTED (exit code: $EXIT_CODE) !!!"
    echo

    # Find most recent checkpoint
    LAST_CHECKPOINT=$(ls -t "$CHECKPOINT_DIR"/*.sav 2>/dev/null | head -1)

    if [ -z "$LAST_CHECKPOINT" ]; then
        echo "ERROR: No checkpoints found (test crashed before first checkpoint)"
        echo "Try reducing checkpoint interval or check if emulator can run at all"
        exit 1
    fi

    echo "Most recent checkpoint: $LAST_CHECKPOINT"
    echo
    echo "To debug, run:"
    echo "  $EMULATOR --restore '$LAST_CHECKPOINT' -T -v '$FIRMWARE'"
    echo
    echo "This will resume from the checkpoint with full verbose trace enabled."
    echo "You can also use -c <N> to limit cycles after restore, e.g.:"
    echo "  $EMULATOR --restore '$LAST_CHECKPOINT' -T -c 100000 '$FIRMWARE'"

    # Show tail of log for quick inspection
    echo
    echo "=== Last 50 lines of log ==="
    tail -50 "$LOG_FILE"

    exit 1
fi

echo
echo "=== Test PASSED: $MAX_CYCLES cycles without crash ==="
echo "Checkpoints saved to: $CHECKPOINT_DIR"
ls -lh "$CHECKPOINT_DIR"/*.sav 2>/dev/null || echo "(no checkpoints saved)"

# Show summary statistics from log
echo
echo "=== Summary Statistics ==="
grep -E "\[.*\] ----" "$LOG_FILE" | tail -5 || echo "(no heartbeat messages)"

exit 0

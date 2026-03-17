#!/bin/bash
# Comprehensive FreeRTOS test suite - runs in both single-core and dual-core modes
# Usage: ./run-all-tests.sh [quick|standard|stress]

set -e

MODE="${1:-standard}"
FLEXE="../flexe/build/Release/xtensa-emu.exe"
TEST_DIR="10-freertos-minimal"
FIRMWARE="$TEST_DIR/build/freertos-minimal-test.bin"
ELF="$TEST_DIR/build/freertos-minimal-test.elf"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log() { echo -e "${GREEN}[TEST]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[FAIL]${NC} $*"; }

# Check if firmware exists
if [ ! -f "$FIRMWARE" ] || [ ! -f "$ELF" ]; then
    error "Firmware not found. Build it first:"
    echo "  cd $TEST_DIR && ./build.ps1"
    exit 1
fi

# Test parameters based on mode
case "$MODE" in
    quick)
        CYCLES_SHORT=10000000      # 10M cycles (~0.0625 sec)
        CYCLES_MEDIUM=50000000     # 50M cycles (~0.3125 sec)
        CHECKPOINT_INTERVAL=20000000
        ;;
    standard)
        CYCLES_SHORT=50000000      # 50M cycles
        CYCLES_MEDIUM=100000000    # 100M cycles (~0.625 sec)
        CHECKPOINT_INTERVAL=20000000
        ;;
    stress)
        CYCLES_SHORT=100000000     # 100M cycles
        CYCLES_MEDIUM=500000000    # 500M cycles (~3.125 sec)
        CYCLES_LONG=1000000000     # 1B cycles (~6.25 sec)
        CHECKPOINT_INTERVAL=100000000
        ;;
    *)
        error "Unknown mode: $MODE"
        echo "Usage: $0 [quick|standard|stress]"
        exit 1
        ;;
esac

log "=========================================="
log "FreeRTOS Test Suite - Mode: $MODE"
log "=========================================="
echo ""

# Create results directory
RESULTS_DIR="test-results-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RESULTS_DIR"

run_test() {
    local test_name="$1"
    local flags="$2"
    local cycles="$3"
    local log_file="$RESULTS_DIR/${test_name}.log"

    log "Running: $test_name"
    log "  Cycles: $cycles"
    log "  Flags: $flags"

    if $FLEXE $flags -q -s "$ELF" -c "$cycles" "$FIRMWARE" > "$log_file" 2>&1; then
        log "✅ PASSED: $test_name"
        echo "PASSED" > "$RESULTS_DIR/${test_name}.result"
        return 0
    else
        error "❌ FAILED: $test_name (exit code: $?)"
        echo "FAILED" > "$RESULTS_DIR/${test_name}.result"
        echo ""
        echo "Last 30 lines of output:"
        tail -30 "$log_file"
        echo ""
        return 1
    fi
}

# Test 1: Dual-core quick validation
run_test "01-dualcore-quick" "" "$CYCLES_SHORT"

# Test 2: Single-core quick validation
run_test "02-singlecore-quick" "-1" "$CYCLES_SHORT"

# Test 3: Dual-core with progress heartbeat
run_test "03-dualcore-medium" "-P 10000000" "$CYCLES_MEDIUM"

# Test 4: Single-core with progress heartbeat
run_test "04-singlecore-medium" "-1 -P 10000000" "$CYCLES_MEDIUM"

# Test 5: Dual-core with checkpointing
log "Running: 05-dualcore-checkpoints"
mkdir -p "$RESULTS_DIR/checkpoints-dual"
run_test "05-dualcore-checkpoints" "-P 20000000 --checkpoint-interval $CHECKPOINT_INTERVAL --checkpoint-dir $RESULTS_DIR/checkpoints-dual" "$CYCLES_MEDIUM"

# Test 6: Single-core with checkpointing
log "Running: 06-singlecore-checkpoints"
mkdir -p "$RESULTS_DIR/checkpoints-single"
run_test "06-singlecore-checkpoints" "-1 -P 20000000 --checkpoint-interval $CHECKPOINT_INTERVAL --checkpoint-dir $RESULTS_DIR/checkpoints-single" "$CYCLES_MEDIUM"

# Test 7: Checkpoint restore (dual-core)
if [ -f "$RESULTS_DIR/checkpoints-dual/checkpoint-$CHECKPOINT_INTERVAL.sav" ]; then
    log "Running: 07-dualcore-restore"
    log "  Restoring from checkpoint-$CHECKPOINT_INTERVAL.sav"
    run_test "07-dualcore-restore" "-P 10000000 --restore $RESULTS_DIR/checkpoints-dual/checkpoint-$CHECKPOINT_INTERVAL.sav" "$((CYCLES_MEDIUM / 2))"
else
    warn "Skipping 07-dualcore-restore (no checkpoint found)"
fi

# Test 8: Checkpoint restore (single-core)
if [ -f "$RESULTS_DIR/checkpoints-single/checkpoint-$CHECKPOINT_INTERVAL.sav" ]; then
    log "Running: 08-singlecore-restore"
    log "  Restoring from checkpoint-$CHECKPOINT_INTERVAL.sav"
    run_test "08-singlecore-restore" "-1 -P 10000000 --restore $RESULTS_DIR/checkpoints-single/checkpoint-$CHECKPOINT_INTERVAL.sav" "$((CYCLES_MEDIUM / 2))"
else
    warn "Skipping 08-singlecore-restore (no checkpoint found)"
fi

# Stress tests (only in stress mode)
if [ "$MODE" = "stress" ]; then
    log "Running stress tests..."

    # Test 9: Long-duration dual-core
    run_test "09-dualcore-long" "-P 100000000 --checkpoint-interval $CHECKPOINT_INTERVAL --checkpoint-dir $RESULTS_DIR/checkpoints-dual-long" "$CYCLES_LONG"

    # Test 10: Long-duration single-core
    run_test "10-singlecore-long" "-1 -P 100000000 --checkpoint-interval $CHECKPOINT_INTERVAL --checkpoint-dir $RESULTS_DIR/checkpoints-single-long" "$CYCLES_LONG"
fi

# Summary
echo ""
log "=========================================="
log "Test Summary"
log "=========================================="

PASSED=$(grep -l "PASSED" "$RESULTS_DIR"/*.result 2>/dev/null | wc -l)
FAILED=$(grep -l "FAILED" "$RESULTS_DIR"/*.result 2>/dev/null | wc -l)
TOTAL=$((PASSED + FAILED))

echo "Total:  $TOTAL"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo ""

if [ "$FAILED" -eq 0 ]; then
    log "✅ All tests PASSED!"
    echo "Results saved to: $RESULTS_DIR"
    exit 0
else
    error "❌ $FAILED test(s) FAILED"
    echo "Results saved to: $RESULTS_DIR"
    echo ""
    echo "Failed tests:"
    grep -l "FAILED" "$RESULTS_DIR"/*.result 2>/dev/null | sed 's/.*\//  - /' | sed 's/.result$//'
    exit 1
fi

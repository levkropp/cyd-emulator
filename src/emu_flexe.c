/*
 * emu_flexe.c — Bridge between cyd-emulator and flexe Xtensa interpreter
 *
 * Uses flexe_session for all init/run/cleanup — no duplicated stub
 * management.  Only GUI-specific code lives here: UART log ring,
 * SDL touch bridge, debug pause/continue.
 */

#include "emu_flexe.h"
#include "flexe_session.h"
#include "xtensa.h"
#include "memory.h"
#include "freertos_stubs.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

/* From emu_touch.c */
extern volatile int emu_app_running;
extern bool touch_read(int *x, int *y);

/* From emu_display.c */
extern uint16_t emu_framebuf[];
extern pthread_mutex_t emu_framebuf_mutex;

/* From emu_sdcard.c or emu_main.c */
extern const char *emu_sdcard_path;
extern uint64_t emu_sdcard_size_bytes;

/* From emu_main.c (log ring buffer) */
extern char emu_log_ring[][48];
extern int  emu_log_head;
#define EMU_LOG_LINES 64

/* Debug pause state (cross-thread) */
static pthread_mutex_t debug_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  debug_cond  = PTHREAD_COND_INITIALIZER;
static volatile int    debug_paused = 0;
static volatile int    debug_pause_requested = 0;
static volatile int    cpu_thread_alive = 0; /* 1 while emu_flexe_run() is active */

/* Module state */
static int flexe_active = 0;
static flexe_session_t *session;

/* UART line accumulator */
static char  uart_line[256];
static int   uart_pos = 0;

static void uart_flush_line(void)
{
    if (uart_pos == 0) return;
    uart_line[uart_pos] = '\0';

    /* Copy into log ring (truncate to fit) */
    strncpy(emu_log_ring[emu_log_head], uart_line, 47);
    emu_log_ring[emu_log_head][47] = '\0';
    emu_log_head = (emu_log_head + 1) % EMU_LOG_LINES;

    uart_pos = 0;
}

static void uart_log_cb(void *ctx, uint8_t byte)
{
    (void)ctx;

    /* Also write to stdout for terminal visibility */
    putchar(byte);

    if (byte == '\n' || byte == '\r') {
        uart_flush_line();
        return;
    }
    if (uart_pos < (int)sizeof(uart_line) - 1)
        uart_line[uart_pos++] = (char)byte;
}

/* Bridge callback: read touch state from emu_touch.c */
static int flexe_touch_read(int *x, int *y, void *ctx)
{
    (void)ctx;
    return touch_read(x, y) ? 1 : 0;
}

int emu_flexe_init(const char *bin_path, const char *elf_path)
{
    flexe_session_config_t cfg = {
        .bin_path      = bin_path,
        .elf_path      = elf_path,
        .sdcard_path   = emu_sdcard_path,
        .sdcard_size   = emu_sdcard_size_bytes,
        .initial_sp    = 0x3FFF8000u,
        .uart_cb       = uart_log_cb,
        .framebuf      = emu_framebuf,
        .framebuf_mutex = &emu_framebuf_mutex,
        .framebuf_w    = 320,
        .framebuf_h    = 240,
        .touch_fn      = flexe_touch_read,
    };

    session = flexe_session_create(&cfg);
    if (!session) {
        fprintf(stderr, "flexe: failed to create session\n");
        return -1;
    }

    flexe_active = 1;
    return 0;
}

void emu_flexe_run(void)
{
    xtensa_cpu_t *cpu = flexe_session_cpu(session, 0);
    freertos_stubs_t *frt = flexe_session_frt(session);

    cpu_thread_alive = 1;
    while (emu_app_running && cpu->running) {
        /* Check if pause requested or breakpoint hit */
        if (debug_pause_requested || cpu->breakpoint_hit) {
            int was_bp = cpu->breakpoint_hit;
            pthread_mutex_lock(&debug_mutex);
            debug_paused = 1;
            debug_pause_requested = 0;
            cpu->breakpoint_hit = false;
            /* Signal anyone waiting for pause to take effect */
            pthread_cond_broadcast(&debug_cond);
            /* Wait until continued */
            while (debug_paused && emu_app_running)
                pthread_cond_wait(&debug_cond, &debug_mutex);
            pthread_mutex_unlock(&debug_mutex);
            /* If we stopped at a breakpoint, execute one step with
               breakpoints suppressed so we can move past the BP address */
            if (was_bp && cpu->breakpoint_count > 0) {
                int saved = cpu->breakpoint_count;
                cpu->breakpoint_count = 0;
                xtensa_step(cpu);
                cpu->breakpoint_count = saved;
            }
            continue;
        }

        /* If halted (WAITI), sleep briefly and poll for debug/interrupts */
        if (cpu->halted) {
            usleep(1000);
            /* Try one step to check for pending interrupts */
            xtensa_step(cpu);
            continue;
        }

        uint32_t pc_before = cpu->pc;
        int ran = xtensa_run(cpu, 10000);
        if (ran < 10000 && !cpu->breakpoint_hit && !debug_pause_requested
            && !cpu->halted)
            break;
        /* Detect infinite self-loop (j self) — launch deferred task
         * when boot code reaches a dead end */
        if (cpu->pc == pc_before && frt) {
            uint32_t param;
            uint32_t fn = freertos_stubs_consume_deferred_task(frt, &param);
            if (fn) {
                ar_write(cpu, 1, 0x3FFE0000u);
                ar_write(cpu, 2, param);
                cpu->pc = fn;
                cpu->ps = 0x00040020u;
            }
        }

        /* Preemptive timeslice + core 1 management */
        flexe_session_post_batch(session, 10000);
    }

    cpu_thread_alive = 0;
    /* If someone is waiting for pause, signal them */
    pthread_mutex_lock(&debug_mutex);
    debug_paused = 1; /* treat stopped CPU as paused */
    pthread_cond_broadcast(&debug_cond);
    pthread_mutex_unlock(&debug_mutex);

    /* Flush any partial UART line */
    uart_flush_line();
    fflush(stdout);
}

void emu_flexe_shutdown(void)
{
    if (!flexe_active) return;
    flexe_session_destroy(session);
    session = NULL;
    flexe_active = 0;
}

int emu_flexe_active(void)
{
    return flexe_active;
}

uint32_t emu_flexe_mem_read32(uint32_t addr)
{
    if (!flexe_active) return 0;
    xtensa_mem_t *mem = flexe_session_mem(session);
    return mem ? mem_read32(mem, addr) : 0;
}

uint8_t emu_flexe_mem_read8(uint32_t addr)
{
    if (!flexe_active) return 0;
    xtensa_mem_t *mem = flexe_session_mem(session);
    return mem ? mem_read8(mem, addr) : 0;
}

uint16_t emu_flexe_mem_read16(uint32_t addr)
{
    if (!flexe_active) return 0;
    xtensa_mem_t *mem = flexe_session_mem(session);
    return mem ? mem_read16(mem, addr) : 0;
}

xtensa_cpu_t *emu_flexe_get_cpu(void)
{
    return flexe_active ? flexe_session_cpu(session, 0) : NULL;
}

xtensa_mem_t *emu_flexe_get_mem(void)
{
    return flexe_active ? flexe_session_mem(session) : NULL;
}

const elf_symbols_t *emu_flexe_get_syms(void)
{
    return flexe_active ? flexe_session_syms(session) : NULL;
}

void emu_flexe_debug_break(void)
{
    debug_pause_requested = 1;
}

void emu_flexe_debug_continue(void)
{
    xtensa_cpu_t *cpu = flexe_session_cpu(session, 0);
    if (!cpu) return;
    /* If CPU stopped (not just paused), restart it */
    if (!cpu->running) {
        cpu->running = true;
    }
    if (cpu->halted) {
        cpu->halted = false;
    }
    pthread_mutex_lock(&debug_mutex);
    debug_paused = 0;
    pthread_cond_broadcast(&debug_cond);
    pthread_mutex_unlock(&debug_mutex);
}

int emu_flexe_debug_paused(void)
{
    return debug_paused || !cpu_thread_alive;
}

int emu_flexe_debug_wait_paused(int timeout_ms)
{
    pthread_mutex_lock(&debug_mutex);
    if (!debug_paused) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (!debug_paused) {
            if (pthread_cond_timedwait(&debug_cond, &debug_mutex, &ts) != 0)
                break;
        }
    }
    int result = debug_paused;
    pthread_mutex_unlock(&debug_mutex);
    return result;
}

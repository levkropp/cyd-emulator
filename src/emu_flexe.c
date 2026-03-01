/*
 * emu_flexe.c — Bridge between cyd-emulator and flexe Xtensa interpreter
 *
 * Mirrors the flexe main.c boot sequence to load and execute ESP32
 * firmware binaries. UART output is routed to the SDL info panel's
 * log ring buffer.
 */

#include "emu_flexe.h"
#include "xtensa.h"
#include "memory.h"
#include "loader.h"
#include "peripherals.h"
#include "rom_stubs.h"
#include "elf_symbols.h"
#include "freertos_stubs.h"
#include "esp_timer_stubs.h"
#include "display_stubs.h"
#include "touch_stubs.h"
#include "sdcard_stubs.h"
#include "wifi_stubs.h"
#include "sha_stubs.h"
#include "aes_stubs.h"
#include "mpi_stubs.h"

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

static xtensa_cpu_t       cpu[2];
static xtensa_mem_t      *mem;
static esp32_periph_t    *periph;
static esp32_rom_stubs_t *rom;
static freertos_stubs_t  *frt;
static esp_timer_stubs_t *etimer;
static display_stubs_t   *dstubs;
static touch_stubs_t     *tstubs;
static sdcard_stubs_t    *sstubs;
static wifi_stubs_t      *wstubs;
static sha_stubs_t       *shstubs;
static aes_stubs_t       *astubs;
static mpi_stubs_t       *mstubs;
static elf_symbols_t     *syms;

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
    /* Load ELF symbols if provided */
    syms = NULL;
    if (elf_path) {
        syms = elf_symbols_load(elf_path);
        if (syms)
            printf("  Symbols: %d from %s\n", elf_symbols_count(syms), elf_path);
        else
            fprintf(stderr, "Warning: failed to load symbols from %s\n", elf_path);
    }

    /* Create memory and peripherals */
    mem = mem_create();
    if (!mem) {
        fprintf(stderr, "flexe: failed to allocate memory\n");
        elf_symbols_destroy(syms); syms = NULL;
        return -1;
    }

    periph = periph_create(mem);
    if (!periph) {
        fprintf(stderr, "flexe: failed to create peripherals\n");
        mem_destroy(mem); mem = NULL;
        elf_symbols_destroy(syms); syms = NULL;
        return -1;
    }
    periph_set_uart_callback(periph, uart_log_cb, NULL);

    /* Load firmware */
    load_result_t res = loader_load_bin(mem, bin_path);
    if (res.result != 0) {
        fprintf(stderr, "flexe: load error: %s\n", res.error);
        periph_destroy(periph); periph = NULL;
        mem_destroy(mem); mem = NULL;
        elf_symbols_destroy(syms); syms = NULL;
        return -1;
    }
    printf("  Loaded:  %d segments, entry=0x%08X\n",
           res.segment_count, res.entry_point);

    /* Initialize CPU core 0 */
    xtensa_cpu_init(&cpu[0]);
    xtensa_cpu_reset(&cpu[0]);
    cpu[0].mem = mem;

    /* ROM function stubs */
    rom = rom_stubs_create(&cpu[0]);
    if (!rom) {
        fprintf(stderr, "flexe: failed to create ROM stubs\n");
        periph_destroy(periph); periph = NULL;
        mem_destroy(mem); mem = NULL;
        elf_symbols_destroy(syms); syms = NULL;
        return -1;
    }
    if (syms)
        rom_stubs_hook_symbols(rom, syms);

    /* FreeRTOS stubs */
    frt = freertos_stubs_create(&cpu[0]);
    if (frt && syms)
        freertos_stubs_hook_symbols(frt, syms);

    /* esp_timer stubs */
    etimer = esp_timer_stubs_create(&cpu[0]);
    if (etimer && syms)
        esp_timer_stubs_hook_symbols(etimer, syms);

    /* Display stubs — render to shared framebuffer */
    dstubs = display_stubs_create(&cpu[0]);
    if (dstubs) {
        display_stubs_set_framebuf(dstubs, emu_framebuf, &emu_framebuf_mutex,
                                    320, 240);
        if (syms) {
            display_stubs_hook_symbols(dstubs, syms);
            int espi_n = display_stubs_hook_tft_espi(dstubs, syms);
            int esprite_n = display_stubs_hook_tft_esprite(dstubs, syms);
            int ofr_n = display_stubs_hook_ofr(dstubs, syms);
            printf("  Hooks:   TFT_eSPI=%d, TFT_eSprite=%d, OFR=%d\n",
                   espi_n, esprite_n, ofr_n);
        }
    }

    /* Touch stubs — read from SDL mouse input */
    tstubs = touch_stubs_create(&cpu[0]);
    if (tstubs) {
        touch_stubs_set_state_fn(tstubs, flexe_touch_read, NULL);
        if (syms)
            touch_stubs_hook_symbols(tstubs, syms);
    }

    /* SD card stubs — file-backed image */
    sstubs = sdcard_stubs_create(&cpu[0]);
    if (sstubs) {
        if (emu_sdcard_path)
            sdcard_stubs_set_image(sstubs, emu_sdcard_path);
        if (emu_sdcard_size_bytes > 0)
            sdcard_stubs_set_size(sstubs, emu_sdcard_size_bytes);
        if (syms)
            sdcard_stubs_hook_symbols(sstubs, syms);
    }

    /* SHA hardware accelerator stubs */
    shstubs = sha_stubs_create(&cpu[0]);
    if (shstubs && syms)
        sha_stubs_hook_symbols(shstubs, syms);

    /* AES hardware accelerator stubs */
    astubs = aes_stubs_create(&cpu[0]);
    if (astubs && syms)
        aes_stubs_hook_symbols(astubs, syms);

    /* MPI (RSA) hardware accelerator stubs */
    mstubs = mpi_stubs_create(&cpu[0]);
    if (mstubs && syms)
        mpi_stubs_hook_symbols(mstubs, syms);

    /* WiFi / lwip socket bridge */
    wstubs = wifi_stubs_create(&cpu[0]);
    if (wstubs && syms)
        wifi_stubs_hook_symbols(wstubs, syms);

    /* Set entry point and initial stack pointer */
    if (res.entry_point != 0)
        cpu[0].pc = res.entry_point;
    ar_write(&cpu[0], 1, 0x3FFF8000u);  /* SP in SRAM (above heap region) */

    /* Initialize CPU core 1 */
    xtensa_cpu_init(&cpu[1]);
    xtensa_cpu_reset(&cpu[1]);
    cpu[1].mem = mem;
    cpu[1].core_id = 1;
    cpu[1].prid = 0xABAB;
    cpu[1].running = false;

    /* Attach core 1 to FreeRTOS */
    if (frt) freertos_stubs_attach_cpu(frt, 1, &cpu[1]);

    /* Share pc_hook infrastructure with core 1 */
    cpu[1].pc_hook = cpu[0].pc_hook;
    cpu[1].pc_hook_ctx = cpu[0].pc_hook_ctx;
    cpu[1].pc_hook_bitmap = cpu[0].pc_hook_bitmap;

    flexe_active = 1;
    return 0;
}

void emu_flexe_run(void)
{
    cpu_thread_alive = 1;
    while (emu_app_running && cpu[0].running) {
        /* Check if pause requested or breakpoint hit */
        if (debug_pause_requested || cpu[0].breakpoint_hit) {
            int was_bp = cpu[0].breakpoint_hit;
            pthread_mutex_lock(&debug_mutex);
            debug_paused = 1;
            debug_pause_requested = 0;
            cpu[0].breakpoint_hit = false;
            /* Signal anyone waiting for pause to take effect */
            pthread_cond_broadcast(&debug_cond);
            /* Wait until continued */
            while (debug_paused && emu_app_running)
                pthread_cond_wait(&debug_cond, &debug_mutex);
            pthread_mutex_unlock(&debug_mutex);
            /* If we stopped at a breakpoint, execute one step with
               breakpoints suppressed so we can move past the BP address */
            if (was_bp && cpu[0].breakpoint_count > 0) {
                int saved = cpu[0].breakpoint_count;
                cpu[0].breakpoint_count = 0;
                xtensa_step(&cpu[0]);
                cpu[0].breakpoint_count = saved;
            }
            continue;
        }

        /* If halted (WAITI), sleep briefly and poll for debug/interrupts */
        if (cpu[0].halted) {
            usleep(1000);
            /* Try one step to check for pending interrupts */
            xtensa_step(&cpu[0]);
            continue;
        }

        uint32_t pc_before = cpu[0].pc;
        int ran = xtensa_run(&cpu[0], 10000);
        if (frt) freertos_stubs_check_preempt_core(frt, 0);
        if (ran < 10000 && !cpu[0].breakpoint_hit && !debug_pause_requested
            && !cpu[0].halted)
            break;
        /* Detect infinite self-loop (j self) — start cooperative scheduler
         * or launch deferred task when boot code reaches a dead end */
        if (cpu[0].pc == pc_before && frt) {
            freertos_stubs_start_scheduler(frt);
        }

        /* Check if core 1 should start */
        if (!cpu[1].running &&
            periph_app_cpu_released(periph) &&
            rom_stubs_app_cpu_boot_addr(rom) != 0) {
            cpu[1].pc = rom_stubs_app_cpu_boot_addr(rom);
            cpu[1].running = true;
        }
        /* Run core 1 batch */
        if (cpu[1].running) {
            xtensa_run(&cpu[1], 10000);
            if (frt) freertos_stubs_check_preempt_core(frt, 1);
            /* Sync cycle counts: use max to preserve vTaskDelay fast-forward */
            if (cpu[1].cycle_count > cpu[0].cycle_count) {
                cpu[0].cycle_count = cpu[1].cycle_count;
                cpu[0].virtual_time_us = cpu[1].cycle_count / 160;
            } else {
                cpu[1].cycle_count = cpu[0].cycle_count;
            }
            cpu[1].virtual_time_us = cpu[0].virtual_time_us;
        }
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

    wifi_stubs_destroy(wstubs);      wstubs = NULL;
    sha_stubs_destroy(shstubs);      shstubs = NULL;
    aes_stubs_destroy(astubs);       astubs = NULL;
    mpi_stubs_destroy(mstubs);       mstubs = NULL;
    sdcard_stubs_destroy(sstubs);    sstubs = NULL;
    touch_stubs_destroy(tstubs);     tstubs = NULL;
    display_stubs_destroy(dstubs);   dstubs = NULL;
    esp_timer_stubs_destroy(etimer); etimer = NULL;
    freertos_stubs_destroy(frt);     frt = NULL;
    rom_stubs_destroy(rom);          rom = NULL;
    periph_destroy(periph);          periph = NULL;
    mem_destroy(mem);                mem = NULL;
    elf_symbols_destroy(syms);       syms = NULL;

    flexe_active = 0;
}

int emu_flexe_active(void)
{
    return flexe_active;
}

uint32_t emu_flexe_mem_read32(uint32_t addr)
{
    if (!flexe_active || !mem) return 0;
    return mem_read32(mem, addr);
}

uint8_t emu_flexe_mem_read8(uint32_t addr)
{
    if (!flexe_active || !mem) return 0;
    return mem_read8(mem, addr);
}

uint16_t emu_flexe_mem_read16(uint32_t addr)
{
    if (!flexe_active || !mem) return 0;
    return mem_read16(mem, addr);
}

xtensa_cpu_t *emu_flexe_get_cpu(void)
{
    return flexe_active ? &cpu[0] : NULL;
}

xtensa_mem_t *emu_flexe_get_mem(void)
{
    return flexe_active ? mem : NULL;
}

const elf_symbols_t *emu_flexe_get_syms(void)
{
    return flexe_active ? syms : NULL;
}

void emu_flexe_debug_break(void)
{
    debug_pause_requested = 1;
}

void emu_flexe_debug_continue(void)
{
    /* If CPU stopped (not just paused), restart it */
    if (!cpu[0].running) {
        cpu[0].running = true;
    }
    if (cpu[0].halted) {
        cpu[0].halted = false;
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

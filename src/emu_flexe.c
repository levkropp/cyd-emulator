/*
 * emu_flexe.c — Bridge between cyd-emulator and flexe Xtensa interpreter
 *
 * Mirrors the flexe main.c boot sequence to load and execute ESP32
 * firmware binaries. UART output is routed to the SDL info panel's
 * log ring buffer.
 */

#ifdef EMU_USE_FLEXE

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

#include <stdio.h>
#include <string.h>
#include <pthread.h>

/* From emu_touch.c */
extern volatile int emu_app_running;
extern bool touch_read(int *x, int *y);

/* From emu_display.c */
extern uint16_t emu_framebuf[];
extern pthread_mutex_t emu_framebuf_mutex;

/* From emu_sdcard.c or emu_main.c */
extern const char *emu_sdcard_path;

/* From emu_main.c (log ring buffer) */
extern char emu_log_ring[][48];
extern int  emu_log_head;
#define EMU_LOG_LINES 64

/* Module state */
static int flexe_active = 0;

static xtensa_cpu_t       cpu;
static xtensa_mem_t      *mem;
static esp32_periph_t    *periph;
static esp32_rom_stubs_t *rom;
static freertos_stubs_t  *frt;
static esp_timer_stubs_t *etimer;
static display_stubs_t   *dstubs;
static touch_stubs_t     *tstubs;
static sdcard_stubs_t    *sstubs;
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

    /* Initialize CPU */
    xtensa_cpu_init(&cpu);
    xtensa_cpu_reset(&cpu);
    cpu.mem = mem;

    /* ROM function stubs */
    rom = rom_stubs_create(&cpu);
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
    frt = freertos_stubs_create(&cpu);
    if (frt && syms)
        freertos_stubs_hook_symbols(frt, syms);

    /* esp_timer stubs */
    etimer = esp_timer_stubs_create(&cpu);
    if (etimer && syms)
        esp_timer_stubs_hook_symbols(etimer, syms);

    /* Display stubs — render to shared framebuffer */
    dstubs = display_stubs_create(&cpu);
    if (dstubs) {
        display_stubs_set_framebuf(dstubs, emu_framebuf, &emu_framebuf_mutex,
                                    320, 240);
        if (syms)
            display_stubs_hook_symbols(dstubs, syms);
    }

    /* Touch stubs — read from SDL mouse input */
    tstubs = touch_stubs_create(&cpu);
    if (tstubs) {
        touch_stubs_set_state_fn(tstubs, flexe_touch_read, NULL);
        if (syms)
            touch_stubs_hook_symbols(tstubs, syms);
    }

    /* SD card stubs — file-backed image */
    sstubs = sdcard_stubs_create(&cpu);
    if (sstubs) {
        if (emu_sdcard_path)
            sdcard_stubs_set_image(sstubs, emu_sdcard_path);
        if (syms)
            sdcard_stubs_hook_symbols(sstubs, syms);
    }

    /* Set entry point and initial stack pointer */
    if (res.entry_point != 0)
        cpu.pc = res.entry_point;
    ar_write(&cpu, 1, 0x3FFE0000u);  /* SP in SRAM data */

    flexe_active = 1;
    return 0;
}

void emu_flexe_run(void)
{
    while (emu_app_running && cpu.running && !cpu.halted) {
        int ran = xtensa_run(&cpu, 10000);
        if (ran < 10000) break;
    }

    /* Flush any partial UART line */
    uart_flush_line();
    fflush(stdout);
}

void emu_flexe_shutdown(void)
{
    if (!flexe_active) return;

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

#endif /* EMU_USE_FLEXE */

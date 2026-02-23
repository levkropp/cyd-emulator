/*
 * emu_system.c -- esp_system and esp_random emulation
 *
 * esp_restart() exits the app thread (the emulator relaunches it).
 * esp_random() reads from /dev/urandom.
 * Heap size reports a plausible ESP32 value.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "esp_system.h"
#include "esp_random.h"
#include "esp_log.h"

extern volatile int emu_app_running;

/* ---- esp_system ---- */

esp_reset_reason_t esp_reset_reason(void)
{
    return ESP_RST_POWERON;
}

void esp_restart(void)
{
    ESP_LOGW("system", "esp_restart() called — stopping app thread");
    emu_app_running = 0;
    pthread_exit(NULL);
    /* noreturn: pthread_exit does not return */
    while (1) {}  /* unreachable, satisfies __attribute__((noreturn)) */
}

uint32_t esp_get_free_heap_size(void)
{
    /* Report a plausible ESP32 free heap value */
    return 200 * 1024;  /* 200 KB */
}

uint32_t esp_get_minimum_free_heap_size(void)
{
    return 150 * 1024;  /* 150 KB */
}

/* ---- esp_random ---- */

uint32_t esp_random(void)
{
    uint32_t val;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&val, sizeof(val), 1, f) != 1)
            val = (uint32_t)rand();
        fclose(f);
    } else {
        val = (uint32_t)rand();
    }
    return val;
}

void esp_fill_random(void *buf, size_t len)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(buf, 1, len, f) != len) {
            /* Fallback: fill with rand() */
            unsigned char *p = (unsigned char *)buf;
            for (size_t i = 0; i < len; i++)
                p[i] = (unsigned char)rand();
        }
        fclose(f);
    } else {
        unsigned char *p = (unsigned char *)buf;
        for (size_t i = 0; i < len; i++)
            p[i] = (unsigned char)rand();
    }
}

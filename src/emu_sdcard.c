/*
 * emu_sdcard.c — File-backed sector I/O (implements sdcard.h)
 *
 * Uses a raw disk image file. Sectors are 512 bytes.
 * Respects board profile sd_slots: if 0, sdcard_init() fails.
 *
 * Hardware speed emulation: throttles I/O to match ESP32 SPI SD card
 * timing (20 MHz SPI3 host) unless turbo mode is enabled.
 */

#include "sdcard.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static const char *TAG = "emu_sdcard";

static FILE *sd_file = NULL;
static uint64_t sd_size = 0;

/* Set by emu_main before sdcard_init is called */
const char *emu_sdcard_path = NULL;
uint64_t emu_sdcard_size_bytes = 4ULL * 1024 * 1024 * 1024; /* default 4GB */
int emu_sdcard_enabled = 1;  /* set from board profile sd_slots */

/* Hardware speed emulation: 0=throttled (real speed), 1=turbo (instant) */
int emu_turbo_mode = 0;

/*
 * Throttle I/O to match ESP32 SPI SD card timing.
 * ESP32 SPI3 host at 20 MHz: 400 ns per byte, ~200 µs command overhead.
 */
static void throttle_io(uint32_t sector_count)
{
    if (emu_turbo_mode) return;
    uint64_t ns = 200000 + (uint64_t)sector_count * 512 * 400;
    struct timespec ts = { ns / 1000000000, ns % 1000000000 };
    nanosleep(&ts, NULL);
}

int sdcard_init(void)
{
    if (!emu_sdcard_enabled) {
        ESP_LOGE(TAG, "No SD card slot on this board");
        return -1;
    }

    if (!emu_sdcard_path) {
        ESP_LOGE(TAG, "No SD card image path set (use --sdcard)");
        return -1;
    }

    /* Open or create the image file */
    sd_file = fopen(emu_sdcard_path, "r+b");
    if (!sd_file) {
        sd_file = fopen(emu_sdcard_path, "w+b");
        if (!sd_file) {
            ESP_LOGE(TAG, "Cannot open/create %s", emu_sdcard_path);
            return -1;
        }
    }

    /* Extend to desired size (sparse file) */
    if (ftruncate(fileno(sd_file), (off_t)emu_sdcard_size_bytes) != 0) {
        ESP_LOGE(TAG, "ftruncate failed");
        fclose(sd_file);
        sd_file = NULL;
        return -1;
    }

    sd_size = emu_sdcard_size_bytes;
    ESP_LOGI(TAG, "SD card image: %s (%llu MB)",
             emu_sdcard_path, (unsigned long long)(sd_size / (1024 * 1024)));
    return 0;
}

void sdcard_deinit(void)
{
    if (sd_file) {
        fclose(sd_file);
        sd_file = NULL;
    }
}

uint64_t sdcard_size(void)
{
    return sd_size;
}

uint32_t sdcard_sector_size(void)
{
    return 512;
}

int sdcard_write(uint32_t lba, uint32_t count, const void *data)
{
    if (!sd_file) return -1;
    throttle_io(count);

    uint64_t offset = (uint64_t)lba * 512;
    if (fseeko(sd_file, (off_t)offset, SEEK_SET) != 0)
        return -1;

    size_t n = fwrite(data, 512, count, sd_file);
    return (n == count) ? 0 : -1;
}

int sdcard_read(uint32_t lba, uint32_t count, void *data)
{
    if (!sd_file) return -1;
    throttle_io(count);

    uint64_t offset = (uint64_t)lba * 512;
    if (fseeko(sd_file, (off_t)offset, SEEK_SET) != 0) {
        memset(data, 0, (size_t)count * 512);
        return -1;
    }

    size_t n = fread(data, 512, count, sd_file);
    if (n < count)
        memset((uint8_t *)data + n * 512, 0, (count - n) * 512);
    return 0;
}

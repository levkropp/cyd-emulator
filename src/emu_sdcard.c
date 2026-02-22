/*
 * emu_sdcard.c — File-backed sector I/O (implements sdcard.h)
 *
 * Uses a raw disk image file. Sectors are 512 bytes.
 * The file is created/extended via ftruncate on init.
 */

#include "sdcard.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "emu_sdcard";

static FILE *sd_file = NULL;
static uint64_t sd_size = 0;

/* Set by emu_main before sdcard_init is called */
const char *emu_sdcard_path = NULL;
uint64_t emu_sdcard_size_bytes = 4ULL * 1024 * 1024 * 1024; /* default 4GB */

int sdcard_init(void)
{
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

    uint64_t offset = (uint64_t)lba * 512;
    if (fseeko(sd_file, (off_t)offset, SEEK_SET) != 0)
        return -1;

    size_t n = fwrite(data, 512, count, sd_file);
    return (n == count) ? 0 : -1;
}

int sdcard_read(uint32_t lba, uint32_t count, void *data)
{
    if (!sd_file) return -1;

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

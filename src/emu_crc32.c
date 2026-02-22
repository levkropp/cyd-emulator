/*
 * emu_crc32.c — Software CRC32 matching esp_rom_crc32_le semantics
 *
 * esp_rom_crc32_le(crc, data, len):
 *   - Pass 0 for a fresh CRC32 computation
 *   - Pass previous result to continue an incremental computation
 *   - Internally handles initial/final XOR with 0xFFFFFFFF
 */

#include "esp_rom_crc.h"

static uint32_t crc32_table[256];
static int table_ready = 0;

static void build_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    table_ready = 1;
}

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    if (!table_ready) build_table();

    /* Undo previous final XOR (or set to 0xFFFFFFFF for fresh start) */
    crc ^= 0xFFFFFFFF;

    for (uint32_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);

    /* Final XOR */
    return crc ^ 0xFFFFFFFF;
}

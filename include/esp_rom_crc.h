/*
 * esp_rom_crc.h — CRC32 shim (implemented in emu_crc32.c)
 */
#ifndef ESP_ROM_CRC_H
#define ESP_ROM_CRC_H

#include <stdint.h>
#include <stddef.h>

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);

#endif /* ESP_ROM_CRC_H */

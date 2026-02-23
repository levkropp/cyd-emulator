/*
 * sdcard.h — SD card block I/O API for CYD emulator
 *
 * Implemented by emu_sdcard.c in the emulator (file-backed image).
 */
#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

int sdcard_init(void);
void sdcard_deinit(void);
uint64_t sdcard_size(void);
uint32_t sdcard_sector_size(void);
int sdcard_write(uint32_t lba, uint32_t count, const void *data);
int sdcard_read(uint32_t lba, uint32_t count, void *data);

#endif /* SDCARD_H */

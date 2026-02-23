/*
 * payload.h — Firmware payload API for CYD emulator
 *
 * Implemented by emu_payload.c in the emulator (mmap'd file).
 * The demo app does not use payloads, but this header is required
 * for the emulator core to compile.
 */
#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stdint.h>

#define PAYLOAD_MAX_ARCHES  8
#define PAYLOAD_MAX_FILES   256

struct payload_file {
    char     path[128];
    uint32_t compressed_size;
    uint32_t original_size;
    uint32_t data_offset;
};

struct payload_arch {
    char name[16];
    int  file_count;
    struct payload_file files[PAYLOAD_MAX_FILES];
    uint32_t data_start;
};

int payload_init(void);
int payload_arch_count(void);
const struct payload_arch *payload_get_arch(int index);
const struct payload_arch *payload_get_arch_by_name(const char *name);
const uint8_t *payload_file_data(const struct payload_arch *arch,
                                  const struct payload_file *file);

#endif /* PAYLOAD_H */

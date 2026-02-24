/*
 * emu_flexe.h — Bridge between cyd-emulator and flexe Xtensa interpreter
 *
 * When EMU_USE_FLEXE is defined, provides functions to load and run
 * ESP32 firmware binaries through the Xtensa LX6 emulator.
 * When not defined, provides inline stubs that return failure.
 */

#ifndef EMU_FLEXE_H
#define EMU_FLEXE_H

#include <stdint.h>

#ifdef EMU_USE_FLEXE

int  emu_flexe_init(const char *bin_path, const char *elf_path);
void emu_flexe_run(void);       /* blocks until emu_app_running==0 or cpu stops */
void emu_flexe_shutdown(void);
int  emu_flexe_active(void);    /* 1 if firmware mode */
uint32_t emu_flexe_mem_read32(uint32_t addr);

#else

static inline int  emu_flexe_init(const char *b, const char *e) { (void)b; (void)e; return -1; }
static inline void emu_flexe_run(void) {}
static inline void emu_flexe_shutdown(void) {}
static inline int  emu_flexe_active(void) { return 0; }
static inline uint32_t emu_flexe_mem_read32(uint32_t addr) { (void)addr; return 0; }

#endif

#endif /* EMU_FLEXE_H */

/*
 * emu_flexe.h — Bridge between cyd-emulator and flexe Xtensa interpreter
 *
 * Provides functions to load and run ESP32 firmware binaries through
 * the Xtensa LX6 emulator.
 */

#ifndef EMU_FLEXE_H
#define EMU_FLEXE_H

#include <stdint.h>

/* Forward declarations for flexe types */
typedef struct xtensa_cpu xtensa_cpu_t;
typedef struct xtensa_mem xtensa_mem_t;

int  emu_flexe_init(const char *bin_path, const char *elf_path);
void emu_flexe_run(void);       /* blocks until emu_app_running==0 or cpu stops */
void emu_flexe_shutdown(void);
int  emu_flexe_active(void);    /* 1 if firmware mode */
uint32_t emu_flexe_mem_read32(uint32_t addr);
uint8_t  emu_flexe_mem_read8(uint32_t addr);
uint16_t emu_flexe_mem_read16(uint32_t addr);

/* Debug accessors (CPU thread must be paused) */
xtensa_cpu_t *emu_flexe_get_cpu(void);
xtensa_mem_t *emu_flexe_get_mem(void);

/* Debug pause/resume (cross-thread safe) */
void emu_flexe_debug_break(void);     /* request pause from control thread */
void emu_flexe_debug_continue(void);  /* resume execution */
int  emu_flexe_debug_paused(void);    /* 1 if CPU is paused */
int  emu_flexe_debug_wait_paused(int timeout_ms); /* wait for pause, return 1 if paused */

#endif /* EMU_FLEXE_H */

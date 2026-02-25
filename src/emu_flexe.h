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

#else

static inline int  emu_flexe_init(const char *b, const char *e) { (void)b; (void)e; return -1; }
static inline void emu_flexe_run(void) {}
static inline void emu_flexe_shutdown(void) {}
static inline int  emu_flexe_active(void) { return 0; }
static inline uint32_t emu_flexe_mem_read32(uint32_t addr) { (void)addr; return 0; }
static inline uint8_t  emu_flexe_mem_read8(uint32_t addr)  { (void)addr; return 0; }
static inline uint16_t emu_flexe_mem_read16(uint32_t addr) { (void)addr; return 0; }
static inline xtensa_cpu_t *emu_flexe_get_cpu(void) { return (void*)0; }
static inline xtensa_mem_t *emu_flexe_get_mem(void) { return (void*)0; }
static inline void emu_flexe_debug_break(void) {}
static inline void emu_flexe_debug_continue(void) {}
static inline int  emu_flexe_debug_paused(void) { return 0; }
static inline int  emu_flexe_debug_wait_paused(int t) { (void)t; return 0; }

#endif

#endif /* EMU_FLEXE_H */

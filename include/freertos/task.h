/*
 * task.h — vTaskDelay shim for native emulator
 *
 * Checks emu_app_running and calls pthread_exit() on shutdown,
 * so any while(1) { vTaskDelay(...); } loop exits cleanly.
 */
#ifndef FREERTOS_TASK_H
#define FREERTOS_TASK_H

#include <unistd.h>
#include <pthread.h>

extern volatile int emu_app_running;

static inline void vTaskDelay(unsigned int ms) {
    usleep(ms * 1000);
    if (!emu_app_running)
        pthread_exit(NULL);
}

#endif /* FREERTOS_TASK_H */

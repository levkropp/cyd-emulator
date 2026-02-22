/*
 * task.h — vTaskDelay shim for native emulator
 */
#ifndef FREERTOS_TASK_H
#define FREERTOS_TASK_H

#include <unistd.h>

static inline void vTaskDelay(unsigned int ms) {
    usleep(ms * 1000);
}

#endif /* FREERTOS_TASK_H */

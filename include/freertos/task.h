/*
 * task.h — FreeRTOS task API emulation
 *
 * xTaskCreate/xTaskCreatePinnedToCore map to pthread_create.
 * Priority and core pinning are ignored. Stack depth is ignored
 * (pthreads manage their own stacks).
 */
#ifndef FREERTOS_TASK_H
#define FREERTOS_TASK_H

#include "FreeRTOS.h"

typedef void *TaskHandle_t;

extern volatile int emu_app_running;

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode, const char *pcName,
                       configSTACK_DEPTH_TYPE usStackDepth, void *pvParameters,
                       UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask);

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode, const char *pcName,
                                    configSTACK_DEPTH_TYPE usStackDepth, void *pvParameters,
                                    UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask,
                                    BaseType_t xCoreID);

void vTaskDelete(TaskHandle_t xTask);
void vTaskDelay(TickType_t xTicksToDelay);
void vTaskDelayUntil(TickType_t *pxPreviousWakeTime, TickType_t xTimeIncrement);
TickType_t xTaskGetTickCount(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);

#define taskYIELD() vTaskDelay(1)

/* Shutdown helper — called by emulator on exit to join child tasks */
void emu_freertos_shutdown(void);

#endif /* FREERTOS_TASK_H */

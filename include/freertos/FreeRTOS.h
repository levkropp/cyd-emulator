/*
 * FreeRTOS.h — Base types and constants for FreeRTOS emulation
 *
 * Maps FreeRTOS primitives to pthreads on the host.
 * Priority, core pinning, and stack sizes are ignored.
 */
#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdint.h>
#include <stddef.h>

/* ---- Base types ---- */
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef unsigned int TickType_t;
typedef uint32_t configSTACK_DEPTH_TYPE;
typedef void (*TaskFunction_t)(void *);

/* ---- Constants ---- */
#define pdTRUE   1
#define pdFALSE  0
#define pdPASS   1
#define pdFAIL   0

#define portMAX_DELAY       ((TickType_t)0xFFFFFFFF)
#define portTICK_PERIOD_MS  1
#define portTICK_RATE_MS    1
#define pdMS_TO_TICKS(ms)   ((TickType_t)(ms))
#define pdTICKS_TO_MS(t)    ((uint32_t)(t))

#define tskIDLE_PRIORITY    0
#define configMAX_PRIORITIES 25
#define portNUM_PROCESSORS  2

/* ---- Critical sections (ESP-IDF style: takes portMUX_TYPE*) ---- */
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED  0
#define SPINLOCK_INITIALIZER          0
#define spinlock_t                    portMUX_TYPE

void emu_enter_critical(void);
void emu_exit_critical(void);

#define taskENTER_CRITICAL(mux)       emu_enter_critical()
#define taskEXIT_CRITICAL(mux)        emu_exit_critical()
#define portENTER_CRITICAL(mux)       emu_enter_critical()
#define portEXIT_CRITICAL(mux)        emu_exit_critical()
#define taskENTER_CRITICAL_ISR(mux)   emu_enter_critical()
#define taskEXIT_CRITICAL_ISR(mux)    emu_exit_critical()
#define portENTER_CRITICAL_ISR(mux)   emu_enter_critical()
#define portEXIT_CRITICAL_ISR(mux)    emu_exit_critical()

/* ---- Misc ---- */
#define portYIELD_FROM_ISR(x)  do { (void)(x); } while(0)
#define configASSERT(x)        do { (void)(x); } while(0)

#endif /* FREERTOS_H */

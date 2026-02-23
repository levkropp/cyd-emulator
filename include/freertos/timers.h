/*
 * timers.h — FreeRTOS software timer API emulation
 *
 * Single timer thread manages all timers via sorted fire times.
 */
#ifndef FREERTOS_TIMERS_H
#define FREERTOS_TIMERS_H

#include "FreeRTOS.h"

typedef void *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);

TimerHandle_t xTimerCreate(const char *pcTimerName, TickType_t xTimerPeriod,
                            UBaseType_t uxAutoReload, void *pvTimerID,
                            TimerCallbackFunction_t pxCallbackFunction);

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod,
                               TickType_t xTicksToWait);
BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait);
BaseType_t xTimerIsTimerActive(TimerHandle_t xTimer);

void *pvTimerGetTimerID(TimerHandle_t xTimer);
void vTimerSetTimerID(TimerHandle_t xTimer, void *pvNewID);

#endif /* FREERTOS_TIMERS_H */

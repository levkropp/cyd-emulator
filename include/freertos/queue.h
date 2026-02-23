/*
 * queue.h — FreeRTOS queue API emulation
 *
 * Ring buffer with mutex + condvar for blocking send/receive.
 */
#ifndef FREERTOS_QUEUE_H
#define FREERTOS_QUEUE_H

#include "FreeRTOS.h"

typedef void *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue,
                       TickType_t xTicksToWait);
BaseType_t xQueueSendToBack(QueueHandle_t xQueue, const void *pvItemToQueue,
                             TickType_t xTicksToWait);
BaseType_t xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue,
                              TickType_t xTicksToWait);
BaseType_t xQueueOverwrite(QueueHandle_t xQueue, const void *pvItemToQueue);

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
                          TickType_t xTicksToWait);
BaseType_t xQueuePeek(QueueHandle_t xQueue, void *pvBuffer,
                       TickType_t xTicksToWait);

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t xQueue);
BaseType_t xQueueReset(QueueHandle_t xQueue);
void vQueueDelete(QueueHandle_t xQueue);

/* ISR variants (no real ISR context — just non-blocking wrappers) */
BaseType_t xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
                              BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue, void *pvBuffer,
                                 BaseType_t *pxHigherPriorityTaskWoken);

#endif /* FREERTOS_QUEUE_H */

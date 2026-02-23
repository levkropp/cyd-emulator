/*
 * event_groups.h — FreeRTOS event group API emulation
 *
 * Bitmask with mutex + condvar for waiting on bit patterns.
 */
#ifndef FREERTOS_EVENT_GROUPS_H
#define FREERTOS_EVENT_GROUPS_H

#include "FreeRTOS.h"

typedef void *EventGroupHandle_t;
typedef uint32_t EventBits_t;

EventGroupHandle_t xEventGroupCreate(void);

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup,
                                EventBits_t uxBitsToSet);
EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup,
                                  EventBits_t uxBitsToClear);
EventBits_t xEventGroupGetBits(EventGroupHandle_t xEventGroup);

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                 EventBits_t uxBitsToWaitFor,
                                 BaseType_t xClearOnExit,
                                 BaseType_t xWaitForAllBits,
                                 TickType_t xTicksToWait);

void vEventGroupDelete(EventGroupHandle_t xEventGroup);

#endif /* FREERTOS_EVENT_GROUPS_H */

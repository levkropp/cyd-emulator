/*
 * emu_freertos.c — FreeRTOS primitive emulation via pthreads
 *
 * Provides tasks, semaphores, queues, event groups, software timers,
 * critical sections, and tick counting — all mapped to POSIX primitives.
 *
 * Design notes:
 * - Priorities and core pinning are ignored (all threads equal)
 * - Stack depth is ignored (pthreads manage their own stacks)
 * - Blocking waits check emu_app_running every 100ms for clean shutdown
 * - Timer callbacks run in a dedicated timer thread (like FreeRTOS daemon)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_log.h"

static const char *TAG = "freertos";

extern volatile int emu_app_running;

/* ================================================================
 * Tick counter — milliseconds since first call (1 tick = 1 ms)
 * ================================================================ */

static uint64_t boot_time_ms;
static int boot_time_init = 0;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

TickType_t xTaskGetTickCount(void)
{
    if (!boot_time_init) {
        boot_time_ms = now_ms();
        boot_time_init = 1;
    }
    return (TickType_t)(now_ms() - boot_time_ms);
}

/* ================================================================
 * Condvar wait helper with deadline tracking + shutdown check
 *
 * Computes the absolute deadline once, then loops with 100ms chunks
 * checking emu_app_running between sleeps. This avoids resetting
 * the timeout on spurious wakeups.
 *
 * Returns 0 if signaled, ETIMEDOUT if deadline passed.
 * ================================================================ */

struct emu_deadline {
    int infinite;        /* true if portMAX_DELAY */
    struct timespec ts;  /* absolute CLOCK_REALTIME deadline */
};

static void deadline_init(struct emu_deadline *dl, TickType_t ticks)
{
    if (ticks == portMAX_DELAY) {
        dl->infinite = 1;
    } else {
        dl->infinite = 0;
        clock_gettime(CLOCK_REALTIME, &dl->ts);
        dl->ts.tv_sec  += ticks / 1000;
        dl->ts.tv_nsec += (long)(ticks % 1000) * 1000000L;
        if (dl->ts.tv_nsec >= 1000000000L) {
            dl->ts.tv_sec++;
            dl->ts.tv_nsec -= 1000000000L;
        }
    }
}

static int cond_wait_deadline(pthread_cond_t *cond, pthread_mutex_t *mutex,
                              struct emu_deadline *dl)
{
    for (;;) {
        if (!emu_app_running) {
            pthread_mutex_unlock(mutex);
            pthread_exit(NULL);
        }

        /* Compute wait point: min(now + 100ms, deadline) */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);

        if (!dl->infinite) {
            /* Check if deadline already passed */
            if (ts.tv_sec > dl->ts.tv_sec ||
                (ts.tv_sec == dl->ts.tv_sec && ts.tv_nsec >= dl->ts.tv_nsec))
                return ETIMEDOUT;
        }

        ts.tv_nsec += 100000000L;  /* +100ms */
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }

        if (!dl->infinite) {
            if (ts.tv_sec > dl->ts.tv_sec ||
                (ts.tv_sec == dl->ts.tv_sec && ts.tv_nsec > dl->ts.tv_nsec))
                ts = dl->ts;
        }

        int ret = pthread_cond_timedwait(cond, mutex, &ts);
        if (ret == 0) return 0;
    }
}

/* ================================================================
 * Tasks — pthread wrappers
 * ================================================================ */

#define MAX_TASKS 32

struct emu_task {
    pthread_t thread;
    int valid;
};

static struct emu_task task_list[MAX_TASKS];
static pthread_mutex_t task_list_mutex = PTHREAD_MUTEX_INITIALIZER;

struct task_arg {
    TaskFunction_t func;
    void *param;
    int index;
};

static void *task_wrapper(void *arg)
{
    struct task_arg *ta = (struct task_arg *)arg;
    TaskFunction_t func = ta->func;
    void *param = ta->param;
    int index = ta->index;
    free(ta);

    func(param);

    /* Task returned normally — mark as done */
    pthread_mutex_lock(&task_list_mutex);
    task_list[index].valid = 0;
    pthread_mutex_unlock(&task_list_mutex);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t pvTaskCode, const char *pcName,
                       configSTACK_DEPTH_TYPE usStackDepth, void *pvParameters,
                       UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask)
{
    (void)usStackDepth;
    (void)uxPriority;

    pthread_mutex_lock(&task_list_mutex);
    int idx = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (!task_list[i].valid) { idx = i; break; }
    }
    if (idx < 0) {
        pthread_mutex_unlock(&task_list_mutex);
        ESP_LOGE(TAG, "xTaskCreate: too many tasks (max %d)", MAX_TASKS);
        return pdFAIL;
    }

    struct task_arg *ta = malloc(sizeof(*ta));
    if (!ta) {
        pthread_mutex_unlock(&task_list_mutex);
        return pdFAIL;
    }
    ta->func = pvTaskCode;
    ta->param = pvParameters;
    ta->index = idx;

    if (pthread_create(&task_list[idx].thread, NULL, task_wrapper, ta) != 0) {
        free(ta);
        pthread_mutex_unlock(&task_list_mutex);
        ESP_LOGE(TAG, "xTaskCreate: pthread_create failed");
        return pdFAIL;
    }
    task_list[idx].valid = 1;

    if (pxCreatedTask)
        *pxCreatedTask = (TaskHandle_t)(uintptr_t)(idx + 1);  /* 1-based */

    pthread_mutex_unlock(&task_list_mutex);
    ESP_LOGI(TAG, "Task created: %s", pcName ? pcName : "?");
    return pdPASS;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode, const char *pcName,
                                    configSTACK_DEPTH_TYPE usStackDepth,
                                    void *pvParameters, UBaseType_t uxPriority,
                                    TaskHandle_t *pxCreatedTask, BaseType_t xCoreID)
{
    (void)xCoreID;
    return xTaskCreate(pvTaskCode, pcName, usStackDepth, pvParameters,
                       uxPriority, pxCreatedTask);
}

void vTaskDelete(TaskHandle_t xTask)
{
    if (xTask == NULL) {
        /* Delete calling task */
        pthread_t self = pthread_self();
        pthread_mutex_lock(&task_list_mutex);
        for (int i = 0; i < MAX_TASKS; i++) {
            if (task_list[i].valid && pthread_equal(task_list[i].thread, self)) {
                task_list[i].valid = 0;
                pthread_detach(self);
                pthread_mutex_unlock(&task_list_mutex);
                pthread_exit(NULL);
            }
        }
        pthread_mutex_unlock(&task_list_mutex);
        pthread_exit(NULL);
    }

    /* Delete specific task by handle */
    int idx = (int)(uintptr_t)xTask - 1;
    if (idx < 0 || idx >= MAX_TASKS) return;

    pthread_mutex_lock(&task_list_mutex);
    if (task_list[idx].valid) {
        pthread_cancel(task_list[idx].thread);
        pthread_join(task_list[idx].thread, NULL);
        task_list[idx].valid = 0;
    }
    pthread_mutex_unlock(&task_list_mutex);
}

void vTaskDelay(TickType_t xTicksToDelay)
{
    if (xTicksToDelay > 0)
        usleep((useconds_t)xTicksToDelay * 1000);
    if (!emu_app_running)
        pthread_exit(NULL);
}

void vTaskDelayUntil(TickType_t *pxPreviousWakeTime, TickType_t xTimeIncrement)
{
    TickType_t target = *pxPreviousWakeTime + xTimeIncrement;
    TickType_t now = xTaskGetTickCount();
    if (target > now)
        vTaskDelay(target - now);
    *pxPreviousWakeTime = target;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    pthread_t self = pthread_self();
    pthread_mutex_lock(&task_list_mutex);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_list[i].valid && pthread_equal(task_list[i].thread, self)) {
            pthread_mutex_unlock(&task_list_mutex);
            return (TaskHandle_t)(uintptr_t)(i + 1);
        }
    }
    pthread_mutex_unlock(&task_list_mutex);
    return NULL;
}

/* ================================================================
 * Critical sections — single global mutex
 * ================================================================ */

static pthread_mutex_t critical_mutex = PTHREAD_MUTEX_INITIALIZER;

void emu_enter_critical(void)
{
    pthread_mutex_lock(&critical_mutex);
}

void emu_exit_critical(void)
{
    pthread_mutex_unlock(&critical_mutex);
}

/* ================================================================
 * Semaphores — mutex + condvar with count
 * ================================================================ */

enum { SEM_MUTEX, SEM_BINARY, SEM_COUNTING, SEM_RECURSIVE };

struct emu_semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int count;
    int max_count;
    int type;
    /* Recursive mutex tracking */
    pthread_t owner;
    int recurse_count;
};

static SemaphoreHandle_t sem_create(int type, int initial, int max_count)
{
    struct emu_semaphore *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->count = initial;
    s->max_count = max_count;
    s->type = type;
    return (SemaphoreHandle_t)s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return sem_create(SEM_MUTEX, 1, 1);
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    return sem_create(SEM_RECURSIVE, 1, 1);
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return sem_create(SEM_BINARY, 0, 1);
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount,
                                            UBaseType_t uxInitialCount)
{
    return sem_create(SEM_COUNTING, (int)uxInitialCount, (int)uxMaxCount);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
    struct emu_semaphore *s = (struct emu_semaphore *)xSemaphore;
    if (!s) return pdFAIL;

    pthread_mutex_lock(&s->mutex);

    /* Fast path: available immediately */
    if (s->count > 0) {
        s->count--;
        pthread_mutex_unlock(&s->mutex);
        return pdTRUE;
    }

    /* Non-blocking check */
    if (xTicksToWait == 0) {
        pthread_mutex_unlock(&s->mutex);
        return pdFALSE;
    }

    /* Blocking wait with deadline */
    struct emu_deadline dl;
    deadline_init(&dl, xTicksToWait);

    while (s->count <= 0) {
        if (cond_wait_deadline(&s->cond, &s->mutex, &dl) == ETIMEDOUT) {
            pthread_mutex_unlock(&s->mutex);
            return pdFALSE;
        }
    }
    s->count--;
    pthread_mutex_unlock(&s->mutex);
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    struct emu_semaphore *s = (struct emu_semaphore *)xSemaphore;
    if (!s) return pdFAIL;

    pthread_mutex_lock(&s->mutex);
    if (s->count >= s->max_count) {
        pthread_mutex_unlock(&s->mutex);
        return pdFAIL;
    }
    s->count++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    return pdTRUE;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xSemaphore,
                                    TickType_t xTicksToWait)
{
    struct emu_semaphore *s = (struct emu_semaphore *)xSemaphore;
    if (!s) return pdFAIL;

    pthread_t self = pthread_self();
    pthread_mutex_lock(&s->mutex);

    /* Already owned by this thread — just increment */
    if (s->recurse_count > 0 && pthread_equal(s->owner, self)) {
        s->recurse_count++;
        pthread_mutex_unlock(&s->mutex);
        return pdTRUE;
    }

    /* Fast path */
    if (s->count > 0) {
        s->count--;
        s->owner = self;
        s->recurse_count = 1;
        pthread_mutex_unlock(&s->mutex);
        return pdTRUE;
    }

    if (xTicksToWait == 0) {
        pthread_mutex_unlock(&s->mutex);
        return pdFALSE;
    }

    struct emu_deadline dl;
    deadline_init(&dl, xTicksToWait);

    while (s->count <= 0) {
        if (cond_wait_deadline(&s->cond, &s->mutex, &dl) == ETIMEDOUT) {
            pthread_mutex_unlock(&s->mutex);
            return pdFALSE;
        }
    }
    s->count--;
    s->owner = self;
    s->recurse_count = 1;
    pthread_mutex_unlock(&s->mutex);
    return pdTRUE;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xSemaphore)
{
    struct emu_semaphore *s = (struct emu_semaphore *)xSemaphore;
    if (!s) return pdFAIL;

    pthread_mutex_lock(&s->mutex);
    if (s->recurse_count > 0 && pthread_equal(s->owner, pthread_self())) {
        s->recurse_count--;
        if (s->recurse_count == 0) {
            s->count++;
            pthread_cond_signal(&s->cond);
        }
    }
    pthread_mutex_unlock(&s->mutex);
    return pdTRUE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore,
                                  BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xSemaphoreGive(xSemaphore);
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
    struct emu_semaphore *s = (struct emu_semaphore *)xSemaphore;
    if (!s) return;
    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    free(s);
}

/* ================================================================
 * Queues — ring buffer with mutex + condvar
 * ================================================================ */

struct emu_queue {
    pthread_mutex_t mutex;
    pthread_cond_t  cond_recv;   /* signaled when item added */
    pthread_cond_t  cond_send;   /* signaled when item removed */
    uint8_t *buffer;
    size_t item_size;
    size_t capacity;
    size_t head;          /* next write position */
    size_t tail;          /* next read position */
    size_t count;         /* items currently in queue */
};

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
    struct emu_queue *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_recv, NULL);
    pthread_cond_init(&q->cond_send, NULL);
    q->item_size = uxItemSize;
    q->capacity = uxQueueLength;
    q->buffer = calloc(uxQueueLength, uxItemSize > 0 ? uxItemSize : 1);
    return (QueueHandle_t)q;
}

BaseType_t xQueueSendToBack(QueueHandle_t xQueue, const void *pvItemToQueue,
                             TickType_t xTicksToWait)
{
    struct emu_queue *q = (struct emu_queue *)xQueue;
    if (!q) return pdFAIL;

    pthread_mutex_lock(&q->mutex);

    if (q->count >= q->capacity) {
        if (xTicksToWait == 0) {
            pthread_mutex_unlock(&q->mutex);
            return pdFALSE;
        }
        struct emu_deadline dl;
        deadline_init(&dl, xTicksToWait);
        while (q->count >= q->capacity) {
            if (cond_wait_deadline(&q->cond_send, &q->mutex, &dl) == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return pdFALSE;
            }
        }
    }

    if (q->item_size > 0 && pvItemToQueue)
        memcpy(q->buffer + q->head * q->item_size, pvItemToQueue, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->cond_recv);
    pthread_mutex_unlock(&q->mutex);
    return pdTRUE;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue,
                       TickType_t xTicksToWait)
{
    return xQueueSendToBack(xQueue, pvItemToQueue, xTicksToWait);
}

BaseType_t xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue,
                              TickType_t xTicksToWait)
{
    struct emu_queue *q = (struct emu_queue *)xQueue;
    if (!q) return pdFAIL;

    pthread_mutex_lock(&q->mutex);

    if (q->count >= q->capacity) {
        if (xTicksToWait == 0) {
            pthread_mutex_unlock(&q->mutex);
            return pdFALSE;
        }
        struct emu_deadline dl;
        deadline_init(&dl, xTicksToWait);
        while (q->count >= q->capacity) {
            if (cond_wait_deadline(&q->cond_send, &q->mutex, &dl) == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return pdFALSE;
            }
        }
    }

    q->tail = (q->tail == 0) ? q->capacity - 1 : q->tail - 1;
    if (q->item_size > 0 && pvItemToQueue)
        memcpy(q->buffer + q->tail * q->item_size, pvItemToQueue, q->item_size);
    q->count++;
    pthread_cond_signal(&q->cond_recv);
    pthread_mutex_unlock(&q->mutex);
    return pdTRUE;
}

BaseType_t xQueueOverwrite(QueueHandle_t xQueue, const void *pvItemToQueue)
{
    struct emu_queue *q = (struct emu_queue *)xQueue;
    if (!q) return pdFAIL;

    pthread_mutex_lock(&q->mutex);
    if (q->count >= q->capacity) {
        /* Discard oldest item */
        q->tail = (q->tail + 1) % q->capacity;
        q->count--;
    }
    if (q->item_size > 0 && pvItemToQueue)
        memcpy(q->buffer + q->head * q->item_size, pvItemToQueue, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->cond_recv);
    pthread_mutex_unlock(&q->mutex);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
                          TickType_t xTicksToWait)
{
    struct emu_queue *q = (struct emu_queue *)xQueue;
    if (!q) return pdFAIL;

    pthread_mutex_lock(&q->mutex);

    if (q->count == 0) {
        if (xTicksToWait == 0) {
            pthread_mutex_unlock(&q->mutex);
            return pdFALSE;
        }
        struct emu_deadline dl;
        deadline_init(&dl, xTicksToWait);
        while (q->count == 0) {
            if (cond_wait_deadline(&q->cond_recv, &q->mutex, &dl) == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return pdFALSE;
            }
        }
    }

    if (q->item_size > 0 && pvBuffer)
        memcpy(pvBuffer, q->buffer + q->tail * q->item_size, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->cond_send);
    pthread_mutex_unlock(&q->mutex);
    return pdTRUE;
}

BaseType_t xQueuePeek(QueueHandle_t xQueue, void *pvBuffer,
                       TickType_t xTicksToWait)
{
    struct emu_queue *q = (struct emu_queue *)xQueue;
    if (!q) return pdFAIL;

    pthread_mutex_lock(&q->mutex);

    if (q->count == 0) {
        if (xTicksToWait == 0) {
            pthread_mutex_unlock(&q->mutex);
            return pdFALSE;
        }
        struct emu_deadline dl;
        deadline_init(&dl, xTicksToWait);
        while (q->count == 0) {
            if (cond_wait_deadline(&q->cond_recv, &q->mutex, &dl) == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return pdFALSE;
            }
        }
    }

    if (q->item_size > 0 && pvBuffer)
        memcpy(pvBuffer, q->buffer + q->tail * q->item_size, q->item_size);
    /* Don't remove the item */
    pthread_mutex_unlock(&q->mutex);
    return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
    struct emu_queue *q = (struct emu_queue *)xQueue;
    if (!q) return 0;
    pthread_mutex_lock(&q->mutex);
    size_t n = q->count;
    pthread_mutex_unlock(&q->mutex);
    return (UBaseType_t)n;
}

UBaseType_t uxQueueSpacesAvailable(QueueHandle_t xQueue)
{
    struct emu_queue *q = (struct emu_queue *)xQueue;
    if (!q) return 0;
    pthread_mutex_lock(&q->mutex);
    size_t n = q->capacity - q->count;
    pthread_mutex_unlock(&q->mutex);
    return (UBaseType_t)n;
}

BaseType_t xQueueReset(QueueHandle_t xQueue)
{
    struct emu_queue *q = (struct emu_queue *)xQueue;
    if (!q) return pdFAIL;
    pthread_mutex_lock(&q->mutex);
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_cond_broadcast(&q->cond_send);
    pthread_mutex_unlock(&q->mutex);
    return pdPASS;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    struct emu_queue *q = (struct emu_queue *)xQueue;
    if (!q) return;
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond_recv);
    pthread_cond_destroy(&q->cond_send);
    free(q->buffer);
    free(q);
}

BaseType_t xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
                              BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xQueueSendToBack(xQueue, pvItemToQueue, 0);
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue, void *pvBuffer,
                                 BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken) *pxHigherPriorityTaskWoken = pdFALSE;
    return xQueueReceive(xQueue, pvBuffer, 0);
}

/* ================================================================
 * Event Groups — bitmask with condvar
 * ================================================================ */

struct emu_event_group {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    EventBits_t bits;
};

EventGroupHandle_t xEventGroupCreate(void)
{
    struct emu_event_group *eg = calloc(1, sizeof(*eg));
    if (!eg) return NULL;
    pthread_mutex_init(&eg->mutex, NULL);
    pthread_cond_init(&eg->cond, NULL);
    return (EventGroupHandle_t)eg;
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup,
                                EventBits_t uxBitsToSet)
{
    struct emu_event_group *eg = (struct emu_event_group *)xEventGroup;
    if (!eg) return 0;
    pthread_mutex_lock(&eg->mutex);
    eg->bits |= uxBitsToSet;
    EventBits_t result = eg->bits;
    pthread_cond_broadcast(&eg->cond);
    pthread_mutex_unlock(&eg->mutex);
    return result;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup,
                                  EventBits_t uxBitsToClear)
{
    struct emu_event_group *eg = (struct emu_event_group *)xEventGroup;
    if (!eg) return 0;
    pthread_mutex_lock(&eg->mutex);
    EventBits_t old = eg->bits;
    eg->bits &= ~uxBitsToClear;
    pthread_mutex_unlock(&eg->mutex);
    return old;
}

EventBits_t xEventGroupGetBits(EventGroupHandle_t xEventGroup)
{
    struct emu_event_group *eg = (struct emu_event_group *)xEventGroup;
    if (!eg) return 0;
    pthread_mutex_lock(&eg->mutex);
    EventBits_t result = eg->bits;
    pthread_mutex_unlock(&eg->mutex);
    return result;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                 EventBits_t uxBitsToWaitFor,
                                 BaseType_t xClearOnExit,
                                 BaseType_t xWaitForAllBits,
                                 TickType_t xTicksToWait)
{
    struct emu_event_group *eg = (struct emu_event_group *)xEventGroup;
    if (!eg) return 0;

    pthread_mutex_lock(&eg->mutex);

    /* Check immediately */
    EventBits_t match = eg->bits & uxBitsToWaitFor;
    int satisfied = xWaitForAllBits ? (match == uxBitsToWaitFor) : (match != 0);

    if (satisfied) {
        EventBits_t result = eg->bits;
        if (xClearOnExit) eg->bits &= ~uxBitsToWaitFor;
        pthread_mutex_unlock(&eg->mutex);
        return result;
    }

    if (xTicksToWait == 0) {
        EventBits_t result = eg->bits;
        pthread_mutex_unlock(&eg->mutex);
        return result;
    }

    struct emu_deadline dl;
    deadline_init(&dl, xTicksToWait);

    for (;;) {
        if (cond_wait_deadline(&eg->cond, &eg->mutex, &dl) == ETIMEDOUT) {
            EventBits_t result = eg->bits;
            pthread_mutex_unlock(&eg->mutex);
            return result;
        }

        match = eg->bits & uxBitsToWaitFor;
        satisfied = xWaitForAllBits ? (match == uxBitsToWaitFor) : (match != 0);
        if (satisfied) {
            EventBits_t result = eg->bits;
            if (xClearOnExit) eg->bits &= ~uxBitsToWaitFor;
            pthread_mutex_unlock(&eg->mutex);
            return result;
        }
    }
}

void vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    struct emu_event_group *eg = (struct emu_event_group *)xEventGroup;
    if (!eg) return;
    pthread_mutex_destroy(&eg->mutex);
    pthread_cond_destroy(&eg->cond);
    free(eg);
}

/* ================================================================
 * Software Timers — dedicated timer thread
 * ================================================================ */

#define MAX_TIMERS 16

struct emu_timer {
    char name[16];
    TickType_t period;
    int auto_reload;
    void *timer_id;
    TimerCallbackFunction_t callback;
    int active;
    uint64_t next_fire_ms;
};

static struct emu_timer timers[MAX_TIMERS];
static int timer_count = 0;
static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;
static pthread_t timer_thread_id;
static int timer_thread_started = 0;

static void *timer_thread_func(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&timer_mutex);

    while (emu_app_running) {
        /* Find earliest active timer */
        uint64_t earliest = UINT64_MAX;
        for (int i = 0; i < timer_count; i++) {
            if (timers[i].active && timers[i].next_fire_ms < earliest)
                earliest = timers[i].next_fire_ms;
        }

        if (earliest == UINT64_MAX) {
            /* No active timers — sleep in 100ms chunks */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&timer_cond, &timer_mutex, &ts);
            continue;
        }

        uint64_t now = now_ms();
        if (now < earliest) {
            uint64_t wait_ms = earliest - now;
            if (wait_ms > 100) wait_ms = 100;
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += wait_ms / 1000;
            ts.tv_nsec += (long)(wait_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&timer_cond, &timer_mutex, &ts);
            continue;
        }

        /* Fire expired timers */
        for (int i = 0; i < timer_count; i++) {
            if (!timers[i].active || !timers[i].callback) continue;
            if (now_ms() < timers[i].next_fire_ms) continue;

            TimerCallbackFunction_t cb = timers[i].callback;
            TimerHandle_t handle = (TimerHandle_t)(uintptr_t)(i + 1);

            if (timers[i].auto_reload) {
                timers[i].next_fire_ms = now_ms() + timers[i].period;
            } else {
                timers[i].active = 0;
            }

            /* Unlock while calling callback to avoid deadlock */
            pthread_mutex_unlock(&timer_mutex);
            cb(handle);
            pthread_mutex_lock(&timer_mutex);
        }
    }

    pthread_mutex_unlock(&timer_mutex);
    return NULL;
}

static void ensure_timer_thread(void)
{
    if (!timer_thread_started) {
        timer_thread_started = 1;
        pthread_create(&timer_thread_id, NULL, timer_thread_func, NULL);
    }
}

TimerHandle_t xTimerCreate(const char *pcTimerName, TickType_t xTimerPeriod,
                            UBaseType_t uxAutoReload, void *pvTimerID,
                            TimerCallbackFunction_t pxCallbackFunction)
{
    pthread_mutex_lock(&timer_mutex);
    if (timer_count >= MAX_TIMERS) {
        pthread_mutex_unlock(&timer_mutex);
        ESP_LOGE(TAG, "xTimerCreate: too many timers (max %d)", MAX_TIMERS);
        return NULL;
    }

    int idx = timer_count++;
    memset(&timers[idx], 0, sizeof(timers[idx]));
    if (pcTimerName)
        strncpy(timers[idx].name, pcTimerName, sizeof(timers[idx].name) - 1);
    timers[idx].period = xTimerPeriod;
    timers[idx].auto_reload = (int)uxAutoReload;
    timers[idx].timer_id = pvTimerID;
    timers[idx].callback = pxCallbackFunction;

    ensure_timer_thread();
    pthread_mutex_unlock(&timer_mutex);
    return (TimerHandle_t)(uintptr_t)(idx + 1);
}

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    int idx = (int)(uintptr_t)xTimer - 1;
    if (idx < 0 || idx >= timer_count) return pdFAIL;

    pthread_mutex_lock(&timer_mutex);
    timers[idx].active = 1;
    timers[idx].next_fire_ms = now_ms() + timers[idx].period;
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&timer_mutex);
    return pdPASS;
}

BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    int idx = (int)(uintptr_t)xTimer - 1;
    if (idx < 0 || idx >= timer_count) return pdFAIL;

    pthread_mutex_lock(&timer_mutex);
    timers[idx].active = 0;
    pthread_mutex_unlock(&timer_mutex);
    return pdPASS;
}

BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    return xTimerStart(xTimer, xTicksToWait);
}

BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod,
                               TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    int idx = (int)(uintptr_t)xTimer - 1;
    if (idx < 0 || idx >= timer_count) return pdFAIL;

    pthread_mutex_lock(&timer_mutex);
    timers[idx].period = xNewPeriod;
    if (timers[idx].active)
        timers[idx].next_fire_ms = now_ms() + xNewPeriod;
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&timer_mutex);
    return pdPASS;
}

BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    int idx = (int)(uintptr_t)xTimer - 1;
    if (idx < 0 || idx >= timer_count) return pdFAIL;

    pthread_mutex_lock(&timer_mutex);
    timers[idx].active = 0;
    timers[idx].callback = NULL;
    pthread_mutex_unlock(&timer_mutex);
    return pdPASS;
}

BaseType_t xTimerIsTimerActive(TimerHandle_t xTimer)
{
    int idx = (int)(uintptr_t)xTimer - 1;
    if (idx < 0 || idx >= timer_count) return pdFALSE;
    return timers[idx].active ? pdTRUE : pdFALSE;
}

void *pvTimerGetTimerID(TimerHandle_t xTimer)
{
    int idx = (int)(uintptr_t)xTimer - 1;
    if (idx < 0 || idx >= timer_count) return NULL;
    return timers[idx].timer_id;
}

void vTimerSetTimerID(TimerHandle_t xTimer, void *pvNewID)
{
    int idx = (int)(uintptr_t)xTimer - 1;
    if (idx < 0 || idx >= timer_count) return;
    timers[idx].timer_id = pvNewID;
}

/* ================================================================
 * Shutdown — join all tracked tasks and stop timer thread
 * ================================================================ */

void emu_freertos_shutdown(void)
{
    /* Stop timer thread */
    if (timer_thread_started) {
        pthread_mutex_lock(&timer_mutex);
        pthread_cond_signal(&timer_cond);
        pthread_mutex_unlock(&timer_mutex);
        pthread_join(timer_thread_id, NULL);
        timer_thread_started = 0;
    }

    /* Join all tracked task threads */
    for (int i = 0; i < MAX_TASKS; i++) {
        pthread_mutex_lock(&task_list_mutex);
        if (task_list[i].valid) {
            pthread_t t = task_list[i].thread;
            task_list[i].valid = 0;
            pthread_mutex_unlock(&task_list_mutex);
            pthread_join(t, NULL);
        } else {
            pthread_mutex_unlock(&task_list_mutex);
        }
    }
}

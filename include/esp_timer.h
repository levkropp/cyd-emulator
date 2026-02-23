/*
 * esp_timer.h -- High-resolution timer API shim
 *
 * Maps to a dedicated timer thread with microsecond resolution.
 * Callbacks run in the timer thread context (not ISR).
 */
#ifndef ESP_TIMER_H
#define ESP_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"  /* for esp_err_t */

typedef struct esp_timer *esp_timer_handle_t;

typedef void (*esp_timer_cb_t)(void *arg);

typedef enum {
    ESP_TIMER_TASK,  /* callback runs in timer task context */
    ESP_TIMER_ISR,   /* (not supported in emulator, treated as TASK) */
    ESP_TIMER_MAX,
} esp_timer_dispatch_t;

typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    esp_timer_dispatch_t dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args,
                            esp_timer_handle_t *out_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
bool esp_timer_is_active(esp_timer_handle_t timer);

/* Returns microseconds since boot */
int64_t esp_timer_get_time(void);

#endif /* ESP_TIMER_H */

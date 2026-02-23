/*
 * emu_timer.c -- esp_timer API emulation
 *
 * Dedicated timer thread manages a list of timers with microsecond
 * fire times.  Callbacks run in the timer thread context.
 * Uses CLOCK_MONOTONIC for timing.
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include "esp_timer.h"
#include "esp_log.h"

extern volatile int emu_app_running;

static const char *TAG = "esp_timer";

/* ---- Time helpers ---- */

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + (int64_t)ts.tv_nsec / 1000;
}

static int64_t boot_time_us = 0;
static int boot_time_init = 0;

int64_t esp_timer_get_time(void)
{
    if (!boot_time_init) {
        boot_time_us = now_us();
        boot_time_init = 1;
    }
    return now_us() - boot_time_us;
}

/* ---- Timer structure ---- */

struct esp_timer {
    esp_timer_cb_t callback;
    void *arg;
    const char *name;
    int active;
    int periodic;
    uint64_t period_us;
    int64_t fire_time_us;  /* absolute time (CLOCK_MONOTONIC based) */
};

/* ---- Timer list ---- */

#define MAX_ESP_TIMERS 32

static struct esp_timer *timer_list[MAX_ESP_TIMERS];
static int timer_count = 0;
static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t timer_cond = PTHREAD_COND_INITIALIZER;
static pthread_t timer_thread;
static int timer_thread_running = 0;

/* ---- Timer thread ---- */

static void *timer_thread_func(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&timer_mutex);

    while (timer_thread_running && emu_app_running) {
        /* Find earliest active timer */
        int64_t earliest = INT64_MAX;
        int earliest_idx = -1;

        for (int i = 0; i < timer_count; i++) {
            if (timer_list[i] && timer_list[i]->active) {
                if (timer_list[i]->fire_time_us < earliest) {
                    earliest = timer_list[i]->fire_time_us;
                    earliest_idx = i;
                }
            }
        }

        if (earliest_idx < 0) {
            /* No active timers — wait for signal or 100ms check */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100000000;  /* 100ms */
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&timer_cond, &timer_mutex, &ts);
            continue;
        }

        int64_t now = now_us();
        if (earliest > now) {
            /* Sleep until fire time, but cap at 100ms for shutdown checks */
            int64_t wait_us = earliest - now;
            if (wait_us > 100000) wait_us = 100000;

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += (long)(wait_us * 1000);
            while (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&timer_cond, &timer_mutex, &ts);
            continue;
        }

        /* Fire the timer */
        struct esp_timer *t = timer_list[earliest_idx];
        if (!t || !t->active) continue;

        esp_timer_cb_t cb = t->callback;
        void *cb_arg = t->arg;

        if (t->periodic) {
            t->fire_time_us += (int64_t)t->period_us;
        } else {
            t->active = 0;
        }

        /* Unlock while calling callback to avoid deadlock */
        pthread_mutex_unlock(&timer_mutex);
        cb(cb_arg);
        pthread_mutex_lock(&timer_mutex);
    }

    pthread_mutex_unlock(&timer_mutex);
    return NULL;
}

static void ensure_timer_thread(void)
{
    if (timer_thread_running) return;
    timer_thread_running = 1;
    pthread_create(&timer_thread, NULL, timer_thread_func, NULL);
}

/* ---- Public API ---- */

esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args,
                            esp_timer_handle_t *out_handle)
{
    if (!create_args || !out_handle || !create_args->callback)
        return ESP_FAIL;

    struct esp_timer *t = calloc(1, sizeof(*t));
    if (!t) return ESP_FAIL;

    t->callback = create_args->callback;
    t->arg = create_args->arg;
    t->name = create_args->name ? create_args->name : "unnamed";
    t->active = 0;

    pthread_mutex_lock(&timer_mutex);
    if (timer_count >= MAX_ESP_TIMERS) {
        pthread_mutex_unlock(&timer_mutex);
        free(t);
        ESP_LOGE(TAG, "Too many esp_timers (max %d)", MAX_ESP_TIMERS);
        return ESP_FAIL;
    }
    timer_list[timer_count++] = t;
    pthread_mutex_unlock(&timer_mutex);

    *out_handle = t;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    if (!timer) return ESP_FAIL;

    ensure_timer_thread();

    pthread_mutex_lock(&timer_mutex);
    timer->periodic = 0;
    timer->period_us = 0;
    timer->fire_time_us = now_us() + (int64_t)timeout_us;
    timer->active = 1;
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&timer_mutex);

    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us)
{
    if (!timer) return ESP_FAIL;

    ensure_timer_thread();

    pthread_mutex_lock(&timer_mutex);
    timer->periodic = 1;
    timer->period_us = period_us;
    timer->fire_time_us = now_us() + (int64_t)period_us;
    timer->active = 1;
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&timer_mutex);

    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    if (!timer) return ESP_FAIL;

    pthread_mutex_lock(&timer_mutex);
    timer->active = 0;
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&timer_mutex);

    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    if (!timer) return ESP_FAIL;

    pthread_mutex_lock(&timer_mutex);
    timer->active = 0;
    for (int i = 0; i < timer_count; i++) {
        if (timer_list[i] == timer) {
            timer_list[i] = timer_list[--timer_count];
            break;
        }
    }
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&timer_mutex);

    free(timer);
    return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t timer)
{
    if (!timer) return false;
    pthread_mutex_lock(&timer_mutex);
    bool active = timer->active;
    pthread_mutex_unlock(&timer_mutex);
    return active;
}

/* Called by emu_main on shutdown */
void emu_esp_timer_shutdown(void)
{
    if (!timer_thread_running) return;

    pthread_mutex_lock(&timer_mutex);
    timer_thread_running = 0;
    pthread_cond_signal(&timer_cond);
    pthread_mutex_unlock(&timer_mutex);

    pthread_join(timer_thread, NULL);

    /* Free remaining timers */
    for (int i = 0; i < timer_count; i++) {
        free(timer_list[i]);
        timer_list[i] = NULL;
    }
    timer_count = 0;
}

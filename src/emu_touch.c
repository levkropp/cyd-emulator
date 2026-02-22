/*
 * emu_touch.c — SDL2 mouse input (implements touch.h)
 *
 * Mouse button down = finger down. Position scaled by 1/scale_factor.
 * State updated from SDL event loop in emu_main.c.
 *
 * Uses a "pending down" latch so quick clicks aren't lost between polls.
 * Logs all touch events to the console panel ring buffer.
 */

#include "touch.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>

static const char *TAG = "touch";

/* Shared touch state — written by SDL event loop, read by app thread */
static pthread_mutex_t touch_mutex = PTHREAD_MUTEX_INITIALIZER;
static int mouse_down = 0;       /* current physical state */
static int mouse_x = 0;
static int mouse_y = 0;
static int pending_down = 0;     /* latched: set on press, cleared on read */
static int pending_x = 0;
static int pending_y = 0;

/* Shutdown flag — defined in emu_touch.c, extern'd everywhere */
volatile int emu_app_running = 0;

/* Touch event log for panel display */
#define TOUCH_LOG_LINES 8
#define TOUCH_LOG_COLS  40
char emu_touch_log[TOUCH_LOG_LINES][TOUCH_LOG_COLS];
int  emu_touch_log_head = 0;

static void touch_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(emu_touch_log[emu_touch_log_head], TOUCH_LOG_COLS, fmt, ap);
    va_end(ap);
    emu_touch_log_head = (emu_touch_log_head + 1) % TOUCH_LOG_LINES;
}

/* Called from SDL event loop (emu_main.c) */
void emu_touch_update(int down, int x, int y)
{
    pthread_mutex_lock(&touch_mutex);
    if (down && !mouse_down) {
        /* Rising edge: latch the press */
        pending_down = 1;
        pending_x = x;
        pending_y = y;
        touch_log("DOWN (%3d, %3d)", x, y);
        ESP_LOGI(TAG, "DOWN (%d, %d)", x, y);
    }
    if (!down && mouse_down) {
        touch_log("UP   (%3d, %3d)", x, y);
    }
    mouse_x = x;
    mouse_y = y;
    mouse_down = down;
    pthread_mutex_unlock(&touch_mutex);
}

void touch_init(void)
{
    ESP_LOGI(TAG, "Touch initialized (SDL2 mouse input)");
}

bool touch_read(int *x, int *y)
{
    pthread_mutex_lock(&touch_mutex);
    int down = mouse_down;
    if (pending_down) {
        down = 1;
        *x = pending_x;
        *y = pending_y;
        pending_down = 0;  /* consume the latch */
    } else {
        *x = mouse_x;
        *y = mouse_y;
    }
    pthread_mutex_unlock(&touch_mutex);
    return down != 0;
}

void touch_wait_tap(int *x, int *y)
{
    /* Wait for finger down */
    while (emu_app_running) {
        if (touch_read(x, y)) break;
        usleep(20000);
    }
    if (!emu_app_running) {
        pthread_exit(NULL);
    }

    /* Consume the latched press */
    pthread_mutex_lock(&touch_mutex);
    pending_down = 0;
    pthread_mutex_unlock(&touch_mutex);

    /* Wait for finger up */
    int tx = *x, ty = *y;
    while (emu_app_running) {
        int nx, ny;
        if (!touch_read(&nx, &ny)) break;
        tx = nx;
        ty = ny;
        usleep(20000);
    }
    if (!emu_app_running) {
        pthread_exit(NULL);
    }

    *x = tx;
    *y = ty;
    touch_log("TAP  (%3d, %3d)", tx, ty);
    ESP_LOGI(TAG, "TAP (%d, %d)", tx, ty);
}

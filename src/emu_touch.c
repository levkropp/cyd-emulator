/*
 * emu_touch.c — SDL2 mouse input (implements touch.h)
 *
 * Mouse button down = finger down. Position scaled by 1/scale_factor.
 * State updated from SDL event loop in emu_main.c.
 *
 * Uses a "pending down" latch so quick clicks aren't lost between polls:
 * mouse-down is latched and only cleared when the app thread reads it.
 */

#include "touch.h"

#include <pthread.h>
#include <unistd.h>

/* Shared touch state — written by SDL event loop, read by app thread */
static pthread_mutex_t touch_mutex = PTHREAD_MUTEX_INITIALIZER;
static int mouse_down = 0;       /* current physical state */
static int mouse_x = 0;
static int mouse_y = 0;
static int pending_down = 0;     /* latched: set on press, cleared on read */
static int pending_x = 0;
static int pending_y = 0;

/* Flag to break out of blocking waits on shutdown */
volatile int emu_app_running;  /* set by emu_main.c */

/* Called from SDL event loop (emu_main.c) */
void emu_touch_update(int down, int x, int y)
{
    pthread_mutex_lock(&touch_mutex);
    mouse_x = x;
    mouse_y = y;
    if (down && !mouse_down) {
        /* Rising edge: latch the press */
        pending_down = 1;
        pending_x = x;
        pending_y = y;
    }
    mouse_down = down;
    pthread_mutex_unlock(&touch_mutex);
}

void touch_init(void)
{
    /* Nothing to do */
}

bool touch_read(int *x, int *y)
{
    pthread_mutex_lock(&touch_mutex);
    int down = mouse_down;
    /* If there's a latched press the app hasn't seen, report it as down */
    if (pending_down) {
        down = 1;
        *x = pending_x;
        *y = pending_y;
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
    if (!emu_app_running) return;

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
    *x = tx;
    *y = ty;
}

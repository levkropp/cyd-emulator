/*
 * emu_lvgl.c -- LVGL display and input drivers for CYD emulator
 *
 * Provides:
 *   - Display flush callback that writes RGB565 into emu_framebuf
 *   - Pointer input callback that reads from touch_read()
 *   - Tick callback via clock_gettime(CLOCK_MONOTONIC)
 *
 * Called from app thread.  The SDL main loop in emu_main.c renders
 * emu_framebuf to the window -- LVGL rendering goes through the
 * same path as direct framebuffer writes.
 */

#include "lvgl.h"
#include "display.h"
#include "touch.h"

#include <string.h>
#include <pthread.h>
#include <time.h>

/* From emu_display.c */
extern uint16_t emu_framebuf[];
extern pthread_mutex_t emu_framebuf_mutex;

/* ---- Tick provider ---- */

static uint32_t emu_lvgl_tick_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ---- Display flush callback ---- */

static void emu_lvgl_flush_cb(lv_display_t *disp,
                               const lv_area_t *area,
                               uint8_t *px_map)
{
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    int32_t w = x2 - x1 + 1;

    uint16_t *src = (uint16_t *)px_map;

    pthread_mutex_lock(&emu_framebuf_mutex);
    for (int32_t y = y1; y <= y2; y++) {
        if (y < 0 || y >= DISPLAY_HEIGHT) {
            src += w;
            continue;
        }
        int32_t cx1 = x1 < 0 ? 0 : x1;
        int32_t cx2 = x2 >= DISPLAY_WIDTH ? DISPLAY_WIDTH - 1 : x2;
        if (cx1 > cx2) {
            src += w;
            continue;
        }
        int32_t skip = cx1 - x1;
        int32_t cw = cx2 - cx1 + 1;
        memcpy(&emu_framebuf[y * DISPLAY_WIDTH + cx1],
               src + skip, cw * sizeof(uint16_t));
        src += w;
    }
    pthread_mutex_unlock(&emu_framebuf_mutex);

    lv_display_flush_ready(disp);
}

/* ---- Touch input callback ---- */

static void emu_lvgl_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int x, y;
    bool pressed = touch_read(&x, &y);

    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
}

/* ---- Public init function ---- */

void emu_lvgl_init(void)
{
    lv_init();
    lv_tick_set_cb(emu_lvgl_tick_cb);

    /* Create display */
    lv_display_t *disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    /* Draw buffer -- full framebuffer for simplest integration */
    static uint8_t draw_buf[DISPLAY_WIDTH * DISPLAY_HEIGHT * 2];
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, emu_lvgl_flush_cb);

    /* Create pointer input device */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, emu_lvgl_read_cb);
    lv_indev_set_display(indev, disp);
}

void emu_lvgl_deinit(void)
{
    lv_deinit();
}

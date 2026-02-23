/*
 * main.c -- Interactive drawing pad demo for CYD emulator
 *
 * Color palette at the top, touch-to-draw on the canvas below.
 * Demonstrates display and touch APIs without requiring
 * SD card, payload, or any external firmware.
 *
 * No FreeRTOS primitives beyond vTaskDelay -- pure bare-metal style.
 */

#include <stdlib.h>

#include "display.h"
#include "touch.h"
#include "font.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "demo";

/* RGB565 color helpers */
#define RGB565(r, g, b) \
    ((uint16_t)(((r) & 0xF8) << 8 | ((g) & 0xFC) << 3 | ((b) >> 3)))

/* Layout */
#define PALETTE_H   24
#define NUM_COLORS  8
#define SWATCH_W    28
#define CLEAR_X     (NUM_COLORS * SWATCH_W)  /* 224 */
#define CLEAR_W     (DISPLAY_WIDTH - CLEAR_X) /* 96 */
#define CANVAS_Y    PALETTE_H
#define CANVAS_H    (DISPLAY_HEIGHT - PALETTE_H)

/* Brush */
#define BRUSH_SIZE  4

/* Palette colors */
static const uint16_t colors[NUM_COLORS] = {
    0xFFFF,             /* white   */
    0xF800,             /* red     */
    0x07E0,             /* green   */
    0x001F,             /* blue    */
    0xFFE0,             /* yellow  */
    0x07FF,             /* cyan    */
    0xF81F,             /* magenta */
    RGB565(255,165,0),  /* orange  */
};

static const char *color_names[NUM_COLORS] = {
    "W", "R", "G", "B", "Y", "C", "M", "O"
};

static int current_color = 0;

/* UI colors */
#define BG_COLOR    0x0000  /* black */
#define BTN_BG      0x4208  /* dark gray */
#define SEL_BORDER  0xFFFF  /* white selection border */
#define DIM_TEXT    0x7BEF  /* medium gray */

static void draw_palette(void)
{
    /* Background for palette bar */
    display_fill_rect(0, 0, DISPLAY_WIDTH, PALETTE_H, BG_COLOR);

    for (int i = 0; i < NUM_COLORS; i++) {
        int x = i * SWATCH_W;

        if (i == current_color) {
            /* Selection border */
            display_fill_rect(x, 0, SWATCH_W, PALETTE_H, SEL_BORDER);
            display_fill_rect(x + 2, 2, SWATCH_W - 4, PALETTE_H - 4, colors[i]);
        } else {
            /* Color swatch with thin border */
            display_fill_rect(x, 0, SWATCH_W, PALETTE_H, 0x2104);
            display_fill_rect(x + 1, 1, SWATCH_W - 2, PALETTE_H - 2, colors[i]);
        }
    }

    /* CLEAR button */
    display_fill_rect(CLEAR_X, 0, CLEAR_W, PALETTE_H, BTN_BG);
    display_string(CLEAR_X + 16, 4, "CLEAR", 0xFFFF, BTN_BG);
}

void app_main(void)
{
    display_init();
    touch_init();

    ESP_LOGI(TAG, "Drawing pad demo started");

    /* Initial screen */
    display_clear(BG_COLOR);
    draw_palette();

    /* Welcome text on canvas */
    display_string(72, CANVAS_Y + 80, "Touch to draw!", DIM_TEXT, BG_COLOR);
    display_string(56, CANVAS_Y + 104, "Select colors above", DIM_TEXT, BG_COLOR);

    int prev_tx = -1, prev_ty = -1;
    int was_down = 0;

    while (1) {
        int tx, ty;
        if (touch_read(&tx, &ty)) {
            if (ty < PALETTE_H) {
                /* Palette area */
                if (tx >= CLEAR_X) {
                    /* Clear canvas */
                    display_fill_rect(0, CANVAS_Y, DISPLAY_WIDTH, CANVAS_H, BG_COLOR);
                    ESP_LOGI(TAG, "Canvas cleared");
                    prev_tx = -1;
                    prev_ty = -1;
                } else {
                    int idx = tx / SWATCH_W;
                    if (idx >= 0 && idx < NUM_COLORS && idx != current_color) {
                        current_color = idx;
                        draw_palette();
                        ESP_LOGI(TAG, "Color: %s", color_names[idx]);
                    }
                }
            } else {
                /* Draw on canvas */
                if (was_down && prev_tx >= 0) {
                    /* Interpolate between previous and current position */
                    int dx = tx - prev_tx;
                    int dy = ty - prev_ty;
                    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
                    if (steps > 0) {
                        for (int s = 0; s <= steps; s++) {
                            int ix = prev_tx + dx * s / steps;
                            int iy = prev_ty + dy * s / steps;
                            display_fill_rect(ix - BRUSH_SIZE / 2,
                                              iy - BRUSH_SIZE / 2,
                                              BRUSH_SIZE, BRUSH_SIZE,
                                              colors[current_color]);
                        }
                    }
                }
                display_fill_rect(tx - BRUSH_SIZE / 2, ty - BRUSH_SIZE / 2,
                                  BRUSH_SIZE, BRUSH_SIZE,
                                  colors[current_color]);
                prev_tx = tx;
                prev_ty = ty;
            }
            was_down = 1;
        } else {
            was_down = 0;
            prev_tx = -1;
            prev_ty = -1;
        }
        vTaskDelay(10);
    }
}

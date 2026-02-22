/*
 * emu_display.c — SDL2 framebuffer display (implements display.h)
 *
 * Maintains a 320x240 RGB565 framebuffer in memory.
 * The SDL main loop (emu_main.c) reads this buffer under mutex
 * and blits it to an SDL texture for rendering.
 */

#include "display.h"
#include "font.h"

#include <string.h>
#include <pthread.h>

/* Shared framebuffer — accessed from app thread (writes) and SDL thread (reads) */
uint16_t emu_framebuf[DISPLAY_WIDTH * DISPLAY_HEIGHT];
pthread_mutex_t emu_framebuf_mutex = PTHREAD_MUTEX_INITIALIZER;

void display_init(void)
{
    pthread_mutex_lock(&emu_framebuf_mutex);
    memset(emu_framebuf, 0, sizeof(emu_framebuf));
    pthread_mutex_unlock(&emu_framebuf_mutex);
}

void display_clear(uint16_t color)
{
    display_fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_WIDTH)  w = DISPLAY_WIDTH - x;
    if (y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    pthread_mutex_lock(&emu_framebuf_mutex);
    for (int row = y; row < y + h; row++) {
        uint16_t *dst = &emu_framebuf[row * DISPLAY_WIDTH + x];
        for (int i = 0; i < w; i++)
            dst[i] = color;
    }
    pthread_mutex_unlock(&emu_framebuf_mutex);
}

void display_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
    const uint8_t *glyph = font_data[c - FONT_FIRST];

    pthread_mutex_lock(&emu_framebuf_mutex);
    for (int row = 0; row < FONT_HEIGHT; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= DISPLAY_HEIGHT) continue;
        if (x < 0 || x + FONT_WIDTH > DISPLAY_WIDTH) continue;

        uint8_t bits = glyph[row];
        uint16_t *dst = &emu_framebuf[dy * DISPLAY_WIDTH + x];
        for (int col = 0; col < FONT_WIDTH; col++)
            dst[col] = (bits & (0x80 >> col)) ? fg : bg;
    }
    pthread_mutex_unlock(&emu_framebuf_mutex);
}

void display_draw_bitmap1bpp(int x, int y, int w, int h,
                              const uint8_t *bitmap, uint16_t fg, uint16_t bg)
{
    int row_bytes = (w + 7) / 8;

    pthread_mutex_lock(&emu_framebuf_mutex);
    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= DISPLAY_HEIGHT) continue;

        const uint8_t *src = bitmap + row * row_bytes;
        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= DISPLAY_WIDTH) continue;
            int bit = src[col / 8] & (0x80 >> (col & 7));
            emu_framebuf[dy * DISPLAY_WIDTH + dx] = bit ? fg : bg;
        }
    }
    pthread_mutex_unlock(&emu_framebuf_mutex);
}

void display_draw_rgb565_line(int x, int y, int w, const uint16_t *pixels)
{
    if (y < 0 || y >= DISPLAY_HEIGHT || w <= 0) return;
    int skip = 0;
    if (x < 0) { skip = -x; w += x; x = 0; }
    if (x + w > DISPLAY_WIDTH) w = DISPLAY_WIDTH - x;
    if (w <= 0) return;

    pthread_mutex_lock(&emu_framebuf_mutex);
    memcpy(&emu_framebuf[y * DISPLAY_WIDTH + x], pixels + skip,
           w * sizeof(uint16_t));
    pthread_mutex_unlock(&emu_framebuf_mutex);
}

void display_string(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    int cx = x, cy = y;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            cy += FONT_HEIGHT;
            s++;
            continue;
        }
        if (cx + FONT_WIDTH > DISPLAY_WIDTH) {
            cx = 0;
            cy += FONT_HEIGHT;
        }
        if (cy + FONT_HEIGHT > DISPLAY_HEIGHT) break;

        display_char(cx, cy, *s, fg, bg);
        cx += FONT_WIDTH;
        s++;
    }
}

/*
 * display.h — Display API for CYD emulator
 *
 * Defines the framebuffer dimensions and drawing primitives.
 * Implemented by emu_display.c in the emulator.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 240

void display_init(void);
void display_clear(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_char(int x, int y, char c, uint16_t fg, uint16_t bg);
void display_string(int x, int y, const char *s, uint16_t fg, uint16_t bg);
void display_draw_bitmap1bpp(int x, int y, int w, int h,
                              const uint8_t *bitmap, uint16_t fg, uint16_t bg);
void display_draw_rgb565_line(int x, int y, int w, const uint16_t *pixels);

#endif /* DISPLAY_H */

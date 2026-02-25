/*
 * font.h — 8x16 bitmap font for CYD emulator
 *
 * Provides a monospace bitmap font covering printable ASCII (32-126).
 * Each glyph is 8 pixels wide and 16 pixels tall.
 * Each row is one byte, MSB = leftmost pixel.
 */
#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define FONT_WIDTH   8
#define FONT_HEIGHT  16
#define FONT_FIRST   32   /* space */
#define FONT_LAST    126  /* tilde */

extern const uint8_t font_data[][FONT_HEIGHT];

#endif /* FONT_H */

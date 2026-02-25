/*
 * touch.h — Touchscreen API for CYD emulator
 *
 * Implemented by emu_touch.c in the emulator (mouse input).
 */
#ifndef TOUCH_H
#define TOUCH_H

#include <stdbool.h>

void touch_init(void);
bool touch_read(int *x, int *y);
void touch_wait_tap(int *x, int *y);

#endif /* TOUCH_H */

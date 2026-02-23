/*
 * lv_conf.h -- LVGL configuration for CYD emulator
 *
 * RGB565, pthreads, clib malloc.  No built-in drivers
 * (we provide our own display/touch via emu_lvgl.c).
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ---- Color ---- */
#define LV_COLOR_DEPTH 16  /* RGB565 */

/* ---- Memory ---- */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* Built-in heap (not used with CLIB, but define to avoid warnings) */
#define LV_MEM_SIZE (256 * 1024U)

/* ---- HAL ---- */
#define LV_DEF_REFR_PERIOD  16   /* ~60 FPS to match our SDL loop */
#define LV_DPI_DEF          130

/* ---- OS ---- */
#define LV_USE_OS  LV_OS_PTHREAD

/* ---- Drawing ---- */
#define LV_DRAW_BUF_STRIDE_ALIGN    1
#define LV_DRAW_BUF_ALIGN           4
#define LV_USE_DRAW_SW              1
#define LV_DRAW_SW_DRAW_UNIT_CNT    1
#define LV_DRAW_SW_COMPLEX          1
#define LV_USE_DRAW_SW_ASM          LV_DRAW_SW_ASM_NONE
#define LV_USE_NATIVE_HELIUM_ASM    0

/* ---- Logging ---- */
#define LV_USE_LOG      0

/* ---- Fonts ---- */
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   0
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_48   0
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* ---- Widgets (enable core set) ---- */
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CANVAS       0
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMAGE        1
#define LV_USE_LABEL        1
#define LV_USE_LINE         1
#define LV_USE_ROLLER       1
#define LV_USE_SCALE        1
#define LV_USE_SLIDER       1
#define LV_USE_SWITCH       1
#define LV_USE_TABLE        1
#define LV_USE_TEXTAREA     1

/* ---- Extra widgets ---- */
#define LV_USE_CALENDAR     0
#define LV_USE_CHART        1
#define LV_USE_COLORWHEEL   0
#define LV_USE_IMGBTN       0
#define LV_USE_KEYBOARD     1
#define LV_USE_LED          1
#define LV_USE_LIST         1
#define LV_USE_MENU         1
#define LV_USE_MSGBOX       1
#define LV_USE_SPAN         1
#define LV_USE_SPINBOX      1
#define LV_USE_SPINNER      1
#define LV_USE_TABVIEW      1
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0

/* ---- Layouts ---- */
#define LV_USE_FLEX     1
#define LV_USE_GRID     1

/* ---- Demos (disabled -- we write our own) ---- */
#define LV_USE_DEMO_WIDGETS             0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER  0
#define LV_USE_DEMO_BENCHMARK           0
#define LV_USE_DEMO_RENDER              0
#define LV_USE_DEMO_STRESS              0
#define LV_USE_DEMO_MUSIC               0

#endif /* LV_CONF_H */

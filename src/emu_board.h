/*
 * emu_board.h — CYD board profiles for emulation
 *
 * Covers 95%+ of the ESP32 "Cheap Yellow Display" ecosystem.
 * Select with --board <model> or override individual params.
 */
#ifndef EMU_BOARD_H
#define EMU_BOARD_H

#include <string.h>
#include <stdio.h>

/* Chip model IDs (must match esp_chip_info.h) */
#define BOARD_CHIP_ESP32    1
#define BOARD_CHIP_ESP32S3  9

struct board_profile {
    const char *model;
    const char *chip_name;
    int chip_model;       /* BOARD_CHIP_ESP32 or BOARD_CHIP_ESP32S3 */
    int cores;
    const char *display_size;
    int display_width;
    int display_height;
    const char *touch_type;
    int sd_slots;
    int usb_otg;
    const char *usb_type;
};

static const struct board_profile board_profiles[] = {
    /* 2.4" boards */
    { "2432S024R", "ESP32",    BOARD_CHIP_ESP32,   2, "2.4\"", 320, 240,
      "XPT2046 (resistive)",  1, 0, "Micro-USB (UART)" },
    { "2432S024C", "ESP32",    BOARD_CHIP_ESP32,   2, "2.4\"", 320, 240,
      "GT911 (capacitive)",   1, 0, "Micro-USB (UART)" },

    /* 2.8" boards — the classic CYD */
    { "2432S028R", "ESP32",    BOARD_CHIP_ESP32,   2, "2.8\"", 320, 240,
      "XPT2046 (resistive)",  1, 0, "Micro-USB (UART)" },
    { "2432S028C", "ESP32",    BOARD_CHIP_ESP32,   2, "2.8\"", 320, 240,
      "GT911 (capacitive)",   1, 0, "Micro-USB (UART)" },

    /* 3.2" boards */
    { "2432S032R", "ESP32",    BOARD_CHIP_ESP32,   2, "3.2\"", 320, 240,
      "XPT2046 (resistive)",  1, 0, "Micro-USB (UART)" },
    { "2432S032C", "ESP32",    BOARD_CHIP_ESP32,   2, "3.2\"", 320, 240,
      "GT911 (capacitive)",   1, 0, "Micro-USB (UART)" },

    /* 3.5" boards — higher resolution */
    { "3248S035R", "ESP32",    BOARD_CHIP_ESP32,   2, "3.5\"", 480, 320,
      "XPT2046 (resistive)",  1, 0, "Micro-USB (UART)" },
    { "3248S035C", "ESP32",    BOARD_CHIP_ESP32,   2, "3.5\"", 480, 320,
      "GT911 (capacitive)",   1, 0, "Micro-USB (UART)" },

    /* 4.3" ESP32 board */
    { "4827S043C", "ESP32",    BOARD_CHIP_ESP32,   2, "4.3\"", 480, 272,
      "FT5x06 (capacitive)",  1, 0, "Micro-USB (UART)" },

    /* 4.3" ESP32-S3 boards */
    { "8048S043R", "ESP32-S3", BOARD_CHIP_ESP32S3, 2, "4.3\"", 800, 480,
      "XPT2046 (resistive)",  1, 1, "USB-C (OTG)" },
    { "8048S043C", "ESP32-S3", BOARD_CHIP_ESP32S3, 2, "4.3\"", 800, 480,
      "GT911 (capacitive)",   1, 1, "USB-C (OTG)" },

    /* 5.0" ESP32-S3 board */
    { "8048S050C", "ESP32-S3", BOARD_CHIP_ESP32S3, 2, "5.0\"", 800, 480,
      "GT911 (capacitive)",   1, 1, "USB-C (OTG)" },

    /* 7.0" ESP32-S3 board */
    { "8048S070C", "ESP32-S3", BOARD_CHIP_ESP32S3, 2, "7.0\"", 800, 480,
      "GT911 (capacitive)",   1, 1, "USB-C (OTG)" },
};

#define BOARD_COUNT (int)(sizeof(board_profiles) / sizeof(board_profiles[0]))

/* Default board */
#define BOARD_DEFAULT_INDEX 2  /* 2432S028R */

static inline const struct board_profile *board_find(const char *model)
{
    for (int i = 0; i < BOARD_COUNT; i++) {
        if (strcasecmp(board_profiles[i].model, model) == 0)
            return &board_profiles[i];
    }
    return NULL;
}

static inline void board_list(void)
{
    printf("Available CYD board profiles:\n\n");
    printf("  %-12s %-9s %-6s %-10s %-22s %s  %s\n",
           "MODEL", "CHIP", "LCD", "RES", "TOUCH", "SD", "USB");
    printf("  %-12s %-9s %-6s %-10s %-22s %s  %s\n",
           "-----", "----", "---", "---", "-----", "--", "---");
    for (int i = 0; i < BOARD_COUNT; i++) {
        const struct board_profile *b = &board_profiles[i];
        char res[16];
        snprintf(res, sizeof(res), "%dx%d", b->display_width, b->display_height);
        printf("  %-12s %-9s %-6s %-10s %-22s %d   %s%s\n",
               b->model, b->chip_name, b->display_size, res,
               b->touch_type, b->sd_slots, b->usb_type,
               (i == BOARD_DEFAULT_INDEX) ? "  (default)" : "");
    }
}

/* Global active board — set by emu_main.c before app_main() starts */
extern const struct board_profile *emu_active_board;

#endif /* EMU_BOARD_H */

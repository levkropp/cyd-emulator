/*
 * esp_log.h — ESP-IDF logging shim with ring buffer for console panel
 */
#ifndef ESP_LOG_H
#define ESP_LOG_H

#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

/*
 * Ring buffer for panel display.
 * Stores the last EMU_LOG_LINES lines for rendering in the SDL info panel.
 */
#define EMU_LOG_LINES   64
#define EMU_LOG_COLS    48

extern char emu_log_ring[EMU_LOG_LINES][EMU_LOG_COLS];
extern int  emu_log_head;  /* next write position */

static inline void emu_log_append(char level, const char *tag, const char *msg)
{
    /* Write to ring buffer */
    snprintf(emu_log_ring[emu_log_head], EMU_LOG_COLS, "[%c] %.43s", level, msg);
    emu_log_head = (emu_log_head + 1) % EMU_LOG_LINES;
}

#define ESP_LOGE(tag, fmt, ...) do { \
    char _msg[256]; snprintf(_msg, sizeof(_msg), fmt, ##__VA_ARGS__); \
    printf("[E][%s] %s\n", tag, _msg); \
    emu_log_append('E', tag, _msg); \
} while(0)

#define ESP_LOGW(tag, fmt, ...) do { \
    char _msg[256]; snprintf(_msg, sizeof(_msg), fmt, ##__VA_ARGS__); \
    printf("[W][%s] %s\n", tag, _msg); \
    emu_log_append('W', tag, _msg); \
} while(0)

#define ESP_LOGI(tag, fmt, ...) do { \
    char _msg[256]; snprintf(_msg, sizeof(_msg), fmt, ##__VA_ARGS__); \
    printf("[I][%s] %s\n", tag, _msg); \
    emu_log_append('I', tag, _msg); \
} while(0)

#define ESP_ERROR_CHECK(x) do { (void)(x); } while(0)

static inline const char *esp_err_to_name(esp_err_t err) {
    return (err == ESP_OK) ? "ESP_OK" : "ESP_FAIL";
}

#endif /* ESP_LOG_H */

/*
 * esp_system.h -- System API shim
 */
#ifndef ESP_SYSTEM_H
#define ESP_SYSTEM_H

#ifdef _MSC_VER
#include "../flexe/src/msvc_compat.h"
#endif

#include <stdint.h>
#include "esp_log.h"  /* for esp_err_t */

/* Portable noreturn attribute */
#ifdef _MSC_VER
#define ESP_NORETURN __declspec(noreturn)
#else
#define ESP_NORETURN __attribute__((noreturn))
#endif

typedef enum {
    ESP_RST_UNKNOWN   = 0,
    ESP_RST_POWERON   = 1,
    ESP_RST_SW        = 3,
    ESP_RST_PANIC     = 4,
    ESP_RST_INT_WDT   = 5,
    ESP_RST_TASK_WDT  = 6,
    ESP_RST_WDT       = 7,
    ESP_RST_DEEPSLEEP = 8,
    ESP_RST_BROWNOUT  = 9,
    ESP_RST_SDIO      = 10,
} esp_reset_reason_t;

esp_reset_reason_t esp_reset_reason(void);
ESP_NORETURN void esp_restart(void);
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);

#endif /* ESP_SYSTEM_H */

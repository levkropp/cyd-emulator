/*
 * esp_chip_info.h — Chip info stub for native emulator
 */
#ifndef ESP_CHIP_INFO_H
#define ESP_CHIP_INFO_H

#include <stdint.h>

typedef enum {
    CHIP_ESP32   = 1,
    CHIP_ESP32S2 = 2,
    CHIP_ESP32S3 = 9,
    CHIP_ESP32C3 = 5,
} esp_chip_model_t;

#define CHIP_FEATURE_EMB_FLASH  (1 << 0)

typedef struct {
    esp_chip_model_t model;
    uint32_t features;
    uint16_t revision;
    uint8_t  cores;
} esp_chip_info_t;

static inline void esp_chip_info(esp_chip_info_t *info) {
    info->model = CHIP_ESP32;
    info->features = 0;
    info->revision = 0;
    info->cores = 2;
}

#endif /* ESP_CHIP_INFO_H */

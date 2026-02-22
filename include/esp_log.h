/*
 * esp_log.h — ESP-IDF logging shim for native emulator
 */
#ifndef ESP_LOG_H
#define ESP_LOG_H

#include <stdio.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)

#define ESP_ERROR_CHECK(x) do { (void)(x); } while(0)

static inline const char *esp_err_to_name(esp_err_t err) {
    return (err == ESP_OK) ? "ESP_OK" : "ESP_FAIL";
}

#endif /* ESP_LOG_H */

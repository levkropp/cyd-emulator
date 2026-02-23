/*
 * nvs.h -- Non-Volatile Storage API shim
 *
 * File-backed key-value store in ~/.cyd-emulator/nvs/.
 * Each namespace is a separate binary file.
 */
#ifndef NVS_H
#define NVS_H

#include <stdint.h>
#include <stddef.h>
#include "esp_log.h"  /* for esp_err_t */

typedef uint32_t nvs_handle_t;
typedef nvs_handle_t nvs_handle;  /* legacy alias */

typedef enum {
    NVS_READONLY,
    NVS_READWRITE,
} nvs_open_mode_t;

typedef nvs_open_mode_t nvs_open_mode;  /* legacy alias */

#define ESP_ERR_NVS_NOT_FOUND        0x1102
#define ESP_ERR_NVS_INVALID_HANDLE   0x1103
#define ESP_ERR_NVS_INVALID_NAME     0x1104
#define ESP_ERR_NVS_INVALID_LENGTH   0x1105

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                    nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);

/* Setters */
esp_err_t nvs_set_i8(nvs_handle_t handle, const char *key, int8_t value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_set_i16(nvs_handle_t handle, const char *key, int16_t value);
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value);
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value);
esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value);
esp_err_t nvs_set_i64(nvs_handle_t handle, const char *key, int64_t value);
esp_err_t nvs_set_u64(nvs_handle_t handle, const char *key, uint64_t value);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                        const void *value, size_t length);

/* Getters */
esp_err_t nvs_get_i8(nvs_handle_t handle, const char *key, int8_t *out_value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out_value);
esp_err_t nvs_get_i16(nvs_handle_t handle, const char *key, int16_t *out_value);
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *out_value);
esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value);
esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value);
esp_err_t nvs_get_i64(nvs_handle_t handle, const char *key, int64_t *out_value);
esp_err_t nvs_get_u64(nvs_handle_t handle, const char *key, uint64_t *out_value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key,
                       char *out_value, size_t *length);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key,
                        void *out_value, size_t *length);

/* Erase */
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_erase_all(nvs_handle_t handle);

#endif /* NVS_H */

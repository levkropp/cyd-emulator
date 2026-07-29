/* Minimal repro: NVS write triggers spi_flash_op_block_func via esp_ipc_call
 * (flash cache ops block the other CPU). Hangs if the IPC handshake stalls. */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

void app_main(void) {
    ESP_LOGI("T", "nvs_flash_init...");
    esp_err_t err = nvs_flash_init();
    ESP_LOGI("T", "nvs_flash_init -> %s", esp_err_to_name(err));

    nvs_handle_t h;
    err = nvs_open("test", NVS_READWRITE, &h);
    ESP_LOGI("T", "nvs_open -> %s", esp_err_to_name(err));

    err = nvs_set_i32(h, "k", 42);
    ESP_LOGI("T", "nvs_set_i32 -> %s", esp_err_to_name(err));

    ESP_LOGI("T", "nvs_commit (flash write, needs op_block IPC)...");
    err = nvs_commit(h);
    ESP_LOGI("T", "nvs_commit -> %s", esp_err_to_name(err));

    int32_t v = 0;
    err = nvs_get_i32(h, "k", &v);
    ESP_LOGI("T", "nvs_get_i32 -> %s v=%ld", esp_err_to_name(err), (long)v);
    ESP_LOGI("T", "DONE");
    for (;;) vTaskDelay(1000);
}

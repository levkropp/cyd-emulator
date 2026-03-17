/* Minimal FreeRTOS Test Firmware
 *
 * Purpose: Test basic FreeRTOS functionality in cyd-emulator
 * - Task creation
 * - Task switching
 * - Delays
 * - Logging
 *
 * This is the simplest possible FreeRTOS app to validate emulator behavior.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "freertos-test";

// Simple task that prints periodically
void task1(void *pvParameters)
{
    int count = 0;
    ESP_LOGI(TAG, "Task 1 started");

    while (1) {
        ESP_LOGI(TAG, "Task 1: count = %d", count++);
        vTaskDelay(pdMS_TO_TICKS(1000));  // Delay 1 second

        if (count >= 5) {
            ESP_LOGI(TAG, "Task 1: Completed 5 iterations, exiting");
            break;
        }
    }

    ESP_LOGI(TAG, "Task 1 finished");
    vTaskDelete(NULL);
}

// Another simple task
void task2(void *pvParameters)
{
    int count = 0;
    ESP_LOGI(TAG, "Task 2 started");

    while (1) {
        ESP_LOGI(TAG, "Task 2: count = %d", count++);
        vTaskDelay(pdMS_TO_TICKS(1500));  // Delay 1.5 seconds

        if (count >= 3) {
            ESP_LOGI(TAG, "Task 2: Completed 3 iterations, exiting");
            break;
        }
    }

    ESP_LOGI(TAG, "Task 2 finished");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "FreeRTOS Minimal Test Starting");
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // Create two simple tasks
    ESP_LOGI(TAG, "Creating Task 1...");
    xTaskCreate(task1, "task1", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Creating Task 2...");
    xTaskCreate(task2, "task2", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Tasks created. Scheduler running.");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // Main task can exit, FreeRTOS will continue running
    ESP_LOGI(TAG, "app_main() exiting, tasks will continue");
}

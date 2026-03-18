/*
 * LVGL Basic Test Firmware for cyd-emulator
 *
 * Tests LVGL 8.x display flush callback hooks
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "LVGL-TEST";

// Display dimensions (CYD screen)
#define DISP_HOR_RES 320
#define DISP_VER_RES 240

// LVGL display buffer
static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf1[DISP_HOR_RES * 40];  // 40 line buffer
static lv_color_t buf2[DISP_HOR_RES * 40];  // Double buffering

// LVGL tick timer callback — called every 1ms to advance LVGL's internal clock
static void lv_tick_timer_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

// Flush callback - called by LVGL to transfer framebuffer to display
static void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    // In real hardware, this would send data to TFT_eSPI
    // In emulator, our spy stub intercepts this and copies to framebuffer

    // Signal LVGL that flushing is done
    lv_disp_flush_ready(disp_drv);
}

void app_main(void)
{
    ESP_LOGI(TAG, "LVGL Basic Test Starting");
    ESP_LOGI(TAG, "LVGL version: %d.%d.%d", lv_version_major(), lv_version_minor(), lv_version_patch());

    // Initialize LVGL
    lv_init();

    // Start LVGL tick timer (1ms period) — required for LVGL time tracking
    const esp_timer_create_args_t tick_timer_args = {
        .callback = lv_tick_timer_cb,
        .name = "lv_tick"
    };
    esp_timer_handle_t tick_timer;
    esp_timer_create(&tick_timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 1000);  // 1ms = 1000us

    // Initialize display buffer
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, DISP_HOR_RES * 40);

    // Initialize display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_HOR_RES;
    disp_drv.ver_res = DISP_VER_RES;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "LVGL initialized. Creating UI...");

    // Create a simple UI with label and button
    lv_obj_t *scr = lv_scr_act();

    // Set background color
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003050), 0);

    // Create title label
    lv_obj_t *label_title = lv_label_create(scr);
    lv_label_set_text(label_title, "LVGL Test");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 20);

    // Create subtitle
    lv_obj_t *label_sub = lv_label_create(scr);
    lv_label_set_text(label_sub, "CYD Emulator\nFlush Callback Test");
    lv_obj_set_style_text_color(label_sub, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(label_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label_sub, LV_ALIGN_CENTER, 0, -20);

    // Create button
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 120, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Test Button");
    lv_obj_center(btn_label);

    ESP_LOGI(TAG, "UI created. Starting LVGL task loop...");

    // LVGL task loop
    while (1) {
        lv_tick_inc(10);  // Advance LVGL time by 10ms per iteration
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

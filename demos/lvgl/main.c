/*
 * main.c -- LVGL demo for CYD emulator
 *
 * Interactive UI with buttons, slider, label, and a chart.
 * Demonstrates LVGL running inside the emulator with touch input.
 */

#include "lvgl.h"
#include "display.h"
#include "touch.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <unistd.h>

static const char *TAG = "lvgl-demo";

/* Provided by emu_lvgl.c */
extern void emu_lvgl_init(void);
extern void emu_lvgl_deinit(void);

/* ---- UI state ---- */
static lv_obj_t *counter_label;
static lv_obj_t *slider_label;
static int counter = 0;

/* ---- Callbacks ---- */

static void btn_increment_cb(lv_event_t *e)
{
    (void)e;
    counter++;
    char buf[32];
    snprintf(buf, sizeof(buf), "Count: %d", counter);
    lv_label_set_text(counter_label, buf);
    ESP_LOGI(TAG, "Increment: %d", counter);
}

static void btn_decrement_cb(lv_event_t *e)
{
    (void)e;
    counter--;
    char buf[32];
    snprintf(buf, sizeof(buf), "Count: %d", counter);
    lv_label_set_text(counter_label, buf);
    ESP_LOGI(TAG, "Decrement: %d", counter);
}

static void btn_reset_cb(lv_event_t *e)
{
    (void)e;
    counter = 0;
    lv_label_set_text(counter_label, "Count: 0");
    ESP_LOGI(TAG, "Reset");
}

static void slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    char buf[32];
    snprintf(buf, sizeof(buf), "Slider: %d%%", val);
    lv_label_set_text(slider_label, buf);
}

/* ---- Build UI ---- */

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A2E), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "CYD Emulator + LVGL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00CCAA), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    /* Counter label */
    counter_label = lv_label_create(scr);
    lv_label_set_text(counter_label, "Count: 0");
    lv_obj_set_style_text_color(counter_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(counter_label, &lv_font_montserrat_16, 0);
    lv_obj_align(counter_label, LV_ALIGN_TOP_MID, 0, 40);

    /* Button row */
    lv_obj_t *btn_row = lv_obj_create(scr);
    lv_obj_set_size(btn_row, 280, 50);
    lv_obj_align(btn_row, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);

    /* Decrement button */
    lv_obj_t *btn_dec = lv_button_create(btn_row);
    lv_obj_set_size(btn_dec, 70, 36);
    lv_obj_t *lbl_dec = lv_label_create(btn_dec);
    lv_label_set_text(lbl_dec, LV_SYMBOL_MINUS);
    lv_obj_center(lbl_dec);
    lv_obj_add_event_cb(btn_dec, btn_decrement_cb, LV_EVENT_CLICKED, NULL);

    /* Reset button */
    lv_obj_t *btn_rst = lv_button_create(btn_row);
    lv_obj_set_size(btn_rst, 70, 36);
    lv_obj_set_style_bg_color(btn_rst, lv_color_hex(0xCC4444), 0);
    lv_obj_t *lbl_rst = lv_label_create(btn_rst);
    lv_label_set_text(lbl_rst, "RESET");
    lv_obj_center(lbl_rst);
    lv_obj_add_event_cb(btn_rst, btn_reset_cb, LV_EVENT_CLICKED, NULL);

    /* Increment button */
    lv_obj_t *btn_inc = lv_button_create(btn_row);
    lv_obj_set_size(btn_inc, 70, 36);
    lv_obj_t *lbl_inc = lv_label_create(btn_inc);
    lv_label_set_text(lbl_inc, LV_SYMBOL_PLUS);
    lv_obj_center(lbl_inc);
    lv_obj_add_event_cb(btn_inc, btn_increment_cb, LV_EVENT_CLICKED, NULL);

    /* Slider section */
    slider_label = lv_label_create(scr);
    lv_label_set_text(slider_label, "Slider: 50%");
    lv_obj_set_style_text_color(slider_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(slider_label, LV_ALIGN_TOP_MID, 0, 138);

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_width(slider, 200);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 162);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Switch row */
    lv_obj_t *sw_row = lv_obj_create(scr);
    lv_obj_set_size(sw_row, 280, 40);
    lv_obj_align(sw_row, LV_ALIGN_TOP_MID, 0, 192);
    lv_obj_set_flex_flow(sw_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sw_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(sw_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sw_row, 0, 0);
    lv_obj_set_style_pad_all(sw_row, 0, 0);

    lv_obj_t *sw_label = lv_label_create(sw_row);
    lv_label_set_text(sw_label, "LED:");
    lv_obj_set_style_text_color(sw_label, lv_color_hex(0xCCCCCC), 0);

    lv_obj_t *sw = lv_switch_create(sw_row);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
}

/* ---- Entry point ---- */

void app_main(void)
{
    display_init();
    touch_init();

    ESP_LOGI(TAG, "Initializing LVGL");
    emu_lvgl_init();

    ESP_LOGI(TAG, "Creating UI");
    create_ui();

    ESP_LOGI(TAG, "LVGL demo running");

    /* Main loop -- drive LVGL timer handler */
    while (1) {
        uint32_t ms = lv_timer_handler();
        if (ms > 50) ms = 50;
        vTaskDelay(ms > 0 ? ms : 1);
    }
}

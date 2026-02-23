/*
 * driver/ledc.h -- LEDC PWM control shim
 *
 * Duty cycle tracked per channel by emu_gpio.c.
 */
#ifndef DRIVER_LEDC_H
#define DRIVER_LEDC_H

#include <stdint.h>
#include "esp_log.h"  /* esp_err_t, ESP_OK */

typedef enum {
    LEDC_HIGH_SPEED_MODE = 0,
    LEDC_LOW_SPEED_MODE  = 1,
    LEDC_SPEED_MODE_MAX,
} ledc_mode_t;

typedef enum {
    LEDC_TIMER_0 = 0,
    LEDC_TIMER_1 = 1,
    LEDC_TIMER_2 = 2,
    LEDC_TIMER_3 = 3,
    LEDC_TIMER_MAX,
} ledc_timer_t;

typedef enum {
    LEDC_CHANNEL_0 = 0,
    LEDC_CHANNEL_1 = 1,
    LEDC_CHANNEL_2 = 2,
    LEDC_CHANNEL_3 = 3,
    LEDC_CHANNEL_4 = 4,
    LEDC_CHANNEL_5 = 5,
    LEDC_CHANNEL_6 = 6,
    LEDC_CHANNEL_7 = 7,
    LEDC_CHANNEL_MAX,
} ledc_channel_t;

typedef enum {
    LEDC_TIMER_1_BIT  = 1,
    LEDC_TIMER_2_BIT  = 2,
    LEDC_TIMER_3_BIT  = 3,
    LEDC_TIMER_4_BIT  = 4,
    LEDC_TIMER_5_BIT  = 5,
    LEDC_TIMER_6_BIT  = 6,
    LEDC_TIMER_7_BIT  = 7,
    LEDC_TIMER_8_BIT  = 8,
    LEDC_TIMER_9_BIT  = 9,
    LEDC_TIMER_10_BIT = 10,
    LEDC_TIMER_11_BIT = 11,
    LEDC_TIMER_12_BIT = 12,
    LEDC_TIMER_13_BIT = 13,
    LEDC_TIMER_14_BIT = 14,
    LEDC_TIMER_15_BIT = 15,
    LEDC_TIMER_BIT_MAX,
} ledc_timer_bit_t;

typedef enum {
    LEDC_AUTO_CLK = 0,
    LEDC_USE_REF_TICK,
    LEDC_USE_APB_CLK,
    LEDC_USE_RTC8M_CLK,
} ledc_clk_cfg_t;

typedef enum {
    LEDC_INTR_DISABLE = 0,
    LEDC_INTR_FADE_END,
} ledc_intr_type_t;

typedef enum {
    LEDC_FADE_NO_WAIT = 0,
    LEDC_FADE_WAIT_DONE,
} ledc_fade_mode_t;

typedef struct {
    ledc_mode_t      speed_mode;
    ledc_timer_bit_t duty_resolution;
    ledc_timer_t     timer_num;
    uint32_t         freq_hz;
    ledc_clk_cfg_t   clk_cfg;
} ledc_timer_config_t;

typedef struct {
    int              gpio_num;
    ledc_mode_t      speed_mode;
    ledc_channel_t   channel;
    ledc_intr_type_t intr_type;
    ledc_timer_t     timer_sel;
    uint32_t         duty;
    int              hpoint;
} ledc_channel_config_t;

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf);
esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf);
esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty);
esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel);
uint32_t  ledc_get_duty(ledc_mode_t speed_mode, ledc_channel_t channel);
esp_err_t ledc_set_freq(ledc_mode_t speed_mode, ledc_timer_t timer_num, uint32_t freq_hz);
esp_err_t ledc_fade_func_install(int intr_alloc_flags);
esp_err_t ledc_set_fade_with_time(ledc_mode_t speed_mode, ledc_channel_t channel,
                                   uint32_t target_duty, int max_fade_time_ms);
esp_err_t ledc_fade_start(ledc_mode_t speed_mode, ledc_channel_t channel,
                           ledc_fade_mode_t fade_mode);

#endif /* DRIVER_LEDC_H */

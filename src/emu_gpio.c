/*
 * emu_gpio.c -- GPIO, LEDC, and ADC emulation
 *
 * GPIO:  In-memory pin state array (40 pins).
 * LEDC:  Duty cycle tracked per channel (8 channels).
 * ADC:   Returns a fixed midpoint value based on configured width.
 */

#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"
#include "esp_log.h"

static const char *TAG = "emu_gpio";

/* ---- GPIO state ---- */

#define GPIO_PIN_COUNT  40

static uint32_t gpio_levels[GPIO_PIN_COUNT];
static gpio_mode_t gpio_modes[GPIO_PIN_COUNT];

esp_err_t gpio_config(const gpio_config_t *cfg)
{
    if (!cfg) return ESP_FAIL;
    for (int i = 0; i < GPIO_PIN_COUNT; i++) {
        if (cfg->pin_bit_mask & (1ULL << i)) {
            gpio_modes[i] = cfg->mode;
        }
    }
    return ESP_OK;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    if (gpio_num < 0 || gpio_num >= GPIO_PIN_COUNT) return ESP_FAIL;
    gpio_modes[gpio_num] = mode;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    if (gpio_num < 0 || gpio_num >= GPIO_PIN_COUNT) return ESP_FAIL;
    uint32_t prev = gpio_levels[gpio_num];
    gpio_levels[gpio_num] = level ? 1 : 0;
    if (gpio_num == 21 && prev != gpio_levels[gpio_num]) {
        ESP_LOGI(TAG, "Backlight (GPIO21) -> %u", gpio_levels[gpio_num]);
    }
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    if (gpio_num < 0 || gpio_num >= GPIO_PIN_COUNT) return 0;
    return (int)gpio_levels[gpio_num];
}

esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, int mode)
{
    (void)gpio_num; (void)mode;
    return ESP_OK;
}

/* ---- LEDC state ---- */

#define LEDC_CHAN_COUNT  8

static uint32_t ledc_duty[LEDC_CHAN_COUNT];

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf)
{
    (void)timer_conf;
    return ESP_OK;
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{
    if (!ledc_conf) return ESP_FAIL;
    int ch = ledc_conf->channel;
    if (ch >= 0 && ch < LEDC_CHAN_COUNT) {
        ledc_duty[ch] = ledc_conf->duty;
    }
    return ESP_OK;
}

esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty)
{
    (void)speed_mode;
    if (channel < 0 || channel >= LEDC_CHAN_COUNT) return ESP_FAIL;
    ledc_duty[channel] = duty;
    return ESP_OK;
}

esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel)
{
    (void)speed_mode;
    if (channel < 0 || channel >= LEDC_CHAN_COUNT) return ESP_FAIL;
    ESP_LOGI(TAG, "LEDC ch%d duty=%u", channel, ledc_duty[channel]);
    return ESP_OK;
}

uint32_t ledc_get_duty(ledc_mode_t speed_mode, ledc_channel_t channel)
{
    (void)speed_mode;
    if (channel < 0 || channel >= LEDC_CHAN_COUNT) return 0;
    return ledc_duty[channel];
}

esp_err_t ledc_set_freq(ledc_mode_t speed_mode, ledc_timer_t timer_num, uint32_t freq_hz)
{
    (void)speed_mode; (void)timer_num; (void)freq_hz;
    return ESP_OK;
}

esp_err_t ledc_fade_func_install(int intr_alloc_flags)
{
    (void)intr_alloc_flags;
    return ESP_OK;
}

esp_err_t ledc_set_fade_with_time(ledc_mode_t speed_mode, ledc_channel_t channel,
                                   uint32_t target_duty, int max_fade_time_ms)
{
    (void)speed_mode; (void)max_fade_time_ms;
    if (channel >= 0 && channel < LEDC_CHAN_COUNT) {
        ledc_duty[channel] = target_duty;
    }
    return ESP_OK;
}

esp_err_t ledc_fade_start(ledc_mode_t speed_mode, ledc_channel_t channel,
                           ledc_fade_mode_t fade_mode)
{
    (void)speed_mode; (void)channel; (void)fade_mode;
    return ESP_OK;
}

/* ---- ADC state ---- */

static adc_bits_width_t adc_width = ADC_WIDTH_BIT_12;

esp_err_t adc1_config_width(adc_bits_width_t width_bit)
{
    adc_width = width_bit;
    return ESP_OK;
}

esp_err_t adc1_config_channel_atten(adc1_channel_t channel, adc_atten_t atten)
{
    (void)channel; (void)atten;
    return ESP_OK;
}

int adc1_get_raw(adc1_channel_t channel)
{
    (void)channel;
    /* Return midpoint for the configured width */
    switch (adc_width) {
    case ADC_WIDTH_BIT_9:  return 256;   /* midpoint of 0-511 */
    case ADC_WIDTH_BIT_10: return 512;   /* midpoint of 0-1023 */
    case ADC_WIDTH_BIT_11: return 1024;  /* midpoint of 0-2047 */
    case ADC_WIDTH_BIT_12: return 2048;  /* midpoint of 0-4095 */
    default:               return 2048;
    }
}

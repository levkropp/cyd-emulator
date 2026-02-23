/*
 * driver/gpio.h -- GPIO configuration and control shim
 *
 * In-memory pin state tracked by emu_gpio.c.
 */
#ifndef DRIVER_GPIO_H
#define DRIVER_GPIO_H

#include <stdint.h>
#include "esp_log.h"  /* esp_err_t, ESP_OK */

typedef enum {
    GPIO_MODE_DISABLE       = 0,
    GPIO_MODE_INPUT         = 1,
    GPIO_MODE_OUTPUT        = 2,
    GPIO_MODE_OUTPUT_OD     = 3,
    GPIO_MODE_INPUT_OUTPUT  = 4,
    GPIO_MODE_INPUT_OUTPUT_OD = 5,
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLUP_ENABLE  = 1,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_PULLDOWN_ENABLE  = 1,
} gpio_pulldown_t;

typedef enum {
    GPIO_INTR_DISABLE     = 0,
    GPIO_INTR_POSEDGE     = 1,
    GPIO_INTR_NEGEDGE     = 2,
    GPIO_INTR_ANYEDGE     = 3,
    GPIO_INTR_LOW_LEVEL   = 4,
    GPIO_INTR_HIGH_LEVEL  = 5,
} gpio_int_type_t;

typedef enum {
    GPIO_NUM_0  = 0,   GPIO_NUM_1  = 1,   GPIO_NUM_2  = 2,
    GPIO_NUM_3  = 3,   GPIO_NUM_4  = 4,   GPIO_NUM_5  = 5,
    GPIO_NUM_6  = 6,   GPIO_NUM_7  = 7,   GPIO_NUM_8  = 8,
    GPIO_NUM_9  = 9,   GPIO_NUM_10 = 10,  GPIO_NUM_11 = 11,
    GPIO_NUM_12 = 12,  GPIO_NUM_13 = 13,  GPIO_NUM_14 = 14,
    GPIO_NUM_15 = 15,  GPIO_NUM_16 = 16,  GPIO_NUM_17 = 17,
    GPIO_NUM_18 = 18,  GPIO_NUM_19 = 19,  GPIO_NUM_20 = 20,
    GPIO_NUM_21 = 21,  GPIO_NUM_22 = 22,  GPIO_NUM_23 = 23,
    GPIO_NUM_25 = 25,  GPIO_NUM_26 = 26,  GPIO_NUM_27 = 27,
    GPIO_NUM_32 = 32,  GPIO_NUM_33 = 33,  GPIO_NUM_34 = 34,
    GPIO_NUM_35 = 35,  GPIO_NUM_36 = 36,  GPIO_NUM_37 = 37,
    GPIO_NUM_38 = 38,  GPIO_NUM_39 = 39,
    GPIO_NUM_MAX = 40,
} gpio_num_t;

typedef struct {
    uint64_t        pin_bit_mask;
    gpio_mode_t     mode;
    gpio_pullup_t   pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

esp_err_t gpio_config(const gpio_config_t *cfg);
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);
int       gpio_get_level(gpio_num_t gpio_num);
esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, int mode);

static inline void gpio_pad_select_gpio(int gpio_num)
{
    (void)gpio_num;
}

#endif /* DRIVER_GPIO_H */

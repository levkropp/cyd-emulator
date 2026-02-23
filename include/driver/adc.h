/*
 * driver/adc.h -- ADC stubs
 *
 * adc1_get_raw() returns a configurable midpoint value.
 */
#ifndef DRIVER_ADC_H
#define DRIVER_ADC_H

#include <stdint.h>
#include "esp_log.h"  /* esp_err_t, ESP_OK */

typedef enum {
    ADC_WIDTH_BIT_9  = 0,
    ADC_WIDTH_BIT_10 = 1,
    ADC_WIDTH_BIT_11 = 2,
    ADC_WIDTH_BIT_12 = 3,
    ADC_WIDTH_MAX,
} adc_bits_width_t;

typedef enum {
    ADC_ATTEN_DB_0   = 0,
    ADC_ATTEN_DB_2_5 = 1,
    ADC_ATTEN_DB_6   = 2,
    ADC_ATTEN_DB_11  = 3,
    ADC_ATTEN_DB_12  = 3,  /* alias used in newer ESP-IDF */
    ADC_ATTEN_MAX,
} adc_atten_t;

typedef enum {
    ADC1_CHANNEL_0 = 0,
    ADC1_CHANNEL_1 = 1,
    ADC1_CHANNEL_2 = 2,
    ADC1_CHANNEL_3 = 3,
    ADC1_CHANNEL_4 = 4,
    ADC1_CHANNEL_5 = 5,
    ADC1_CHANNEL_6 = 6,
    ADC1_CHANNEL_7 = 7,
    ADC1_CHANNEL_MAX,
} adc1_channel_t;

esp_err_t adc1_config_width(adc_bits_width_t width_bit);
esp_err_t adc1_config_channel_atten(adc1_channel_t channel, adc_atten_t atten);
int       adc1_get_raw(adc1_channel_t channel);

#endif /* DRIVER_ADC_H */

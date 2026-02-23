/*
 * esp_random.h -- Random number generator shim
 *
 * Maps to /dev/urandom on host.
 */
#ifndef ESP_RANDOM_H
#define ESP_RANDOM_H

#include <stdint.h>
#include <stddef.h>

uint32_t esp_random(void);
void esp_fill_random(void *buf, size_t len);

#endif /* ESP_RANDOM_H */

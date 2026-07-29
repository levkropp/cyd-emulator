/* Minimal raw-SPI ILI9341 test: drives the CYD display through SPI2
 * register-level transactions (spi_master polling), exactly the path the
 * flexe SPI display sniffer captures. Fills the screen with 3 color bands. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

#define PIN_SCLK 14
#define PIN_MOSI 13
#define PIN_MISO 12
#define PIN_CS   15
#define PIN_DC   2
#define PIN_RST  4
#define PIN_BL   21

static spi_device_handle_t spi;

static void lcd_cmd(uint8_t cmd) {
    gpio_set_level(PIN_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_data = {cmd}, .flags = SPI_TRANS_USE_TXDATA };
    spi_device_polling_transmit(spi, &t);
}

static void lcd_data(const uint8_t *data, int len) {
    gpio_set_level(PIN_DC, 1);
    spi_transaction_t t = { .length = (size_t)len * 8, .tx_buffer = data };
    spi_device_polling_transmit(spi, &t);
}

static void lcd_data8(uint8_t b) { lcd_data(&b, 1); }

static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_cmd(0x2A); lcd_data8(x0 >> 8); lcd_data8(x0); lcd_data8(x1 >> 8); lcd_data8(x1);
    lcd_cmd(0x2B); lcd_data8(y0 >> 8); lcd_data8(y0); lcd_data8(y1 >> 8); lcd_data8(y1);
    lcd_cmd(0x2C);
}

static void lcd_fill(uint16_t color) {
    uint8_t buf[64];
    for (int i = 0; i < 64; i += 2) { buf[i] = color >> 8; buf[i + 1] = color & 0xFF; }
    lcd_window(0, 0, 239, 319);
    gpio_set_level(PIN_DC, 1);
    for (int i = 0; i < 240 * 320 / 32; i++)
        lcd_data(buf, 64);
}

void app_main(void) {
    gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BL, 1);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCLK, .mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
    };
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 40000000, .mode = 0, .spics_io_num = PIN_CS,
        .queue_size = 1,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    ESP_LOGI("T", "bus_initialize -> %s", esp_err_to_name(err));
    err = spi_bus_add_device(SPI2_HOST, &dev, &spi);
    ESP_LOGI("T", "bus_add_device -> %s", esp_err_to_name(err));

    gpio_set_level(PIN_RST, 0); vTaskDelay(10);
    gpio_set_level(PIN_RST, 1); vTaskDelay(150);

    lcd_cmd(0x01); vTaskDelay(150);          /* SWRESET */
    lcd_cmd(0x11); vTaskDelay(10);           /* SLPOUT */
    lcd_cmd(0x36); lcd_data8(0x20);          /* MADCTL: MV (landscape) */
    lcd_cmd(0x3A); lcd_data8(0x55);          /* COLMOD: 16bpp */
    lcd_cmd(0x29);                           /* DISPON */

    for (;;) {
        lcd_fill(0xF800);  /* red */
        vTaskDelay(200);
        lcd_fill(0x07E0);  /* green */
        vTaskDelay(200);
        lcd_fill(0x001F);  /* blue */
        vTaskDelay(200);
    }
}

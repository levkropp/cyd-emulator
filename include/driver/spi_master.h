/*
 * driver/spi_master.h -- SPI master stubs (compile-only)
 *
 * All functions are static inline, returning ESP_OK.
 */
#ifndef DRIVER_SPI_MASTER_H
#define DRIVER_SPI_MASTER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_log.h"          /* esp_err_t, ESP_OK */
#include "driver/spi_common.h"

typedef struct {
    int  mosi_io_num;
    int  miso_io_num;
    int  sclk_io_num;
    int  quadwp_io_num;
    int  quadhd_io_num;
    int  max_transfer_sz;
} spi_bus_config_t;

typedef struct {
    uint8_t  command_bits;
    uint8_t  address_bits;
    uint8_t  dummy_bits;
    uint8_t  mode;
    uint16_t duty_cycle_pos;
    uint16_t cs_ena_pretrans;
    uint16_t cs_ena_posttrans;
    int      clock_speed_hz;
    int      input_delay_ns;
    int      spics_io_num;
    uint32_t flags;
    int      queue_size;
    void    *pre_cb;
    void    *post_cb;
} spi_device_interface_config_t;

typedef struct {
    uint32_t  flags;
    uint16_t  cmd;
    uint64_t  addr;
    size_t    length;
    size_t    rxlength;
    void     *user;
    union {
        const void *tx_buffer;
        uint8_t     tx_data[4];
    };
    union {
        void    *rx_buffer;
        uint8_t  rx_data[4];
    };
} spi_transaction_t;

typedef struct spi_device_t *spi_device_handle_t;

static inline esp_err_t spi_bus_initialize(spi_host_device_t host,
                                            const spi_bus_config_t *bus_config,
                                            int dma_chan)
{
    (void)host; (void)bus_config; (void)dma_chan;
    return ESP_OK;
}

static inline esp_err_t spi_bus_add_device(spi_host_device_t host,
                                            const spi_device_interface_config_t *dev_config,
                                            spi_device_handle_t *handle)
{
    (void)host; (void)dev_config;
    /* Return a non-NULL dummy handle */
    static int dummy;
    if (handle) *handle = (spi_device_handle_t)&dummy;
    return ESP_OK;
}

static inline esp_err_t spi_device_transmit(spi_device_handle_t handle,
                                             spi_transaction_t *trans_desc)
{
    (void)handle; (void)trans_desc;
    return ESP_OK;
}

static inline esp_err_t spi_bus_remove_device(spi_device_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

static inline esp_err_t spi_bus_free(spi_host_device_t host)
{
    (void)host;
    return ESP_OK;
}

#endif /* DRIVER_SPI_MASTER_H */

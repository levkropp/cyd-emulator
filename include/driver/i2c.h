/*
 * driver/i2c.h -- I2C stubs (compile-only)
 *
 * All functions are static inline, returning ESP_OK or valid handles.
 */
#ifndef DRIVER_I2C_H
#define DRIVER_I2C_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "esp_log.h"  /* esp_err_t, ESP_OK */

typedef enum {
    I2C_MODE_SLAVE  = 0,
    I2C_MODE_MASTER = 1,
} i2c_mode_t;

typedef enum {
    I2C_NUM_0 = 0,
    I2C_NUM_1 = 1,
    I2C_NUM_MAX,
} i2c_port_t;

typedef enum {
    I2C_MASTER_ACK  = 0,
    I2C_MASTER_NACK = 1,
    I2C_MASTER_LAST_NACK = 2,
} i2c_ack_type_t;

typedef struct {
    i2c_mode_t mode;
    int        sda_io_num;
    int        scl_io_num;
    uint32_t   sda_pullup_en;
    uint32_t   scl_pullup_en;
    union {
        struct {
            uint32_t clk_speed;
        } master;
        struct {
            uint8_t  addr_10bit_en;
            uint16_t slave_addr;
        } slave;
    };
    uint32_t   clk_flags;
} i2c_config_t;

typedef void *i2c_cmd_handle_t;

static inline esp_err_t i2c_param_config(i2c_port_t port, const i2c_config_t *cfg)
{
    (void)port; (void)cfg;
    return ESP_OK;
}

static inline esp_err_t i2c_driver_install(i2c_port_t port, i2c_mode_t mode,
                                            size_t slv_rx_buf, size_t slv_tx_buf,
                                            int intr_alloc_flags)
{
    (void)port; (void)mode; (void)slv_rx_buf; (void)slv_tx_buf; (void)intr_alloc_flags;
    return ESP_OK;
}

static inline esp_err_t i2c_driver_delete(i2c_port_t port)
{
    (void)port;
    return ESP_OK;
}

static inline i2c_cmd_handle_t i2c_cmd_link_create(void)
{
    /* Return a non-NULL dummy handle */
    static int dummy;
    return (i2c_cmd_handle_t)&dummy;
}

static inline esp_err_t i2c_master_start(i2c_cmd_handle_t cmd)
{
    (void)cmd;
    return ESP_OK;
}

static inline esp_err_t i2c_master_write_byte(i2c_cmd_handle_t cmd, uint8_t data,
                                                int ack_en)
{
    (void)cmd; (void)data; (void)ack_en;
    return ESP_OK;
}

static inline esp_err_t i2c_master_write(i2c_cmd_handle_t cmd, const uint8_t *data,
                                          size_t data_len, int ack_en)
{
    (void)cmd; (void)data; (void)data_len; (void)ack_en;
    return ESP_OK;
}

static inline esp_err_t i2c_master_read(i2c_cmd_handle_t cmd, uint8_t *data,
                                         size_t data_len, i2c_ack_type_t ack)
{
    (void)cmd; (void)data; (void)data_len; (void)ack;
    return ESP_OK;
}

static inline esp_err_t i2c_master_stop(i2c_cmd_handle_t cmd)
{
    (void)cmd;
    return ESP_OK;
}

static inline esp_err_t i2c_master_cmd_begin(i2c_port_t port, i2c_cmd_handle_t cmd,
                                              uint32_t ticks_to_wait)
{
    (void)port; (void)cmd; (void)ticks_to_wait;
    return ESP_OK;
}

static inline void i2c_cmd_link_delete(i2c_cmd_handle_t cmd)
{
    (void)cmd;
}

#endif /* DRIVER_I2C_H */

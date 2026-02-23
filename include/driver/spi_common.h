/*
 * driver/spi_common.h -- Common SPI definitions
 */
#ifndef DRIVER_SPI_COMMON_H
#define DRIVER_SPI_COMMON_H

#define SPI1_HOST       0
#define SPI2_HOST       1
#define SPI3_HOST       2
#define HSPI_HOST       SPI2_HOST
#define VSPI_HOST       SPI3_HOST
#define SPI_DMA_CH_AUTO 3

typedef int spi_host_device_t;

#endif /* DRIVER_SPI_COMMON_H */

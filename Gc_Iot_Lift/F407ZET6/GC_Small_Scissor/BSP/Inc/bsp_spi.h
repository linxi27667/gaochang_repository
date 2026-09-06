/**
 * @file bsp_spi.h
 * @brief Minimal SPI bus abstraction used by the W25Q BSP driver.
 */
#ifndef __BSP_SPI_H__
#define __BSP_SPI_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 抽象 GPIO 泛型结构体
typedef struct
{
    void *port;
    uint16_t pin;
} spi_gpio_t;

// SPI 总线对象结构体
typedef struct spi_bus_dev
{
    // SPI 外设句柄（STM32 下为 SPI_HandleTypeDef *）
    void *handle;

    // 片选引脚
    spi_gpio_t cs;

    // 硬件底层操作方法指针
    void    (*Init)(void);
    void    (*CS_Write)(void *port, uint16_t pin, uint8_t level);
    uint8_t (*Transmit)(void *hspi, const uint8_t *tx_data, uint16_t size, uint32_t timeout);
    uint8_t (*Transmit_Receive)(void *hspi, const uint8_t *tx_data, uint8_t *rx_data, uint16_t size, uint32_t timeout);
} spi_bus_t;

// SPI 总线对外 API
void SPI_Bus_Init(spi_bus_t *bus);
void SPI_Bus_Select(spi_bus_t *bus);
void SPI_Bus_Deselect(spi_bus_t *bus);

#endif /* __BSP_SPI_H__ */

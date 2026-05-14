#ifndef SIM_BSP_W25QXX_H
#define SIM_BSP_W25QXX_H

#include <stdint.h>

#define W25Q_OK  0
#define W25Q_ERR 1

/* SPI 总线抽象 */
typedef struct { int _; } spi_bus_t;

typedef struct {
    spi_bus_t *bus;
} w25q_t;

uint32_t W25Q_Read_JEDEC_ID(w25q_t *dev);
uint8_t  W25Q_Init_Device(w25q_t *dev);
uint8_t  W25Q_Read_Buffer(w25q_t *dev, uint32_t addr, uint8_t *buf, uint32_t size);
uint8_t  W25Q_Sector_Erase(w25q_t *dev, uint32_t addr);
uint8_t  W25Q_Page_Program(w25q_t *dev, uint32_t addr, const uint8_t *buf, uint32_t size);

#endif

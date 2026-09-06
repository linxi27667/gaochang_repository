#ifndef RISE_STATS_FAKE_BSP_W25QXX_H
#define RISE_STATS_FAKE_BSP_W25QXX_H

#include <stdint.h>
#include "bsp_spi.h"

typedef struct w25q_dev {
    spi_bus_t *bus;
} w25q_t;

#define W25Q_OK 0U
#define W25Q_ERR 1U

uint8_t W25Q_Init_Device(w25q_t *flash);
uint32_t W25Q_Read_JEDEC_ID(w25q_t *flash);
uint16_t W25Q_Read_Device_ID(w25q_t *flash);
uint8_t W25Q_Read_Byte(w25q_t *flash, uint32_t addr);
uint8_t W25Q_Read_Buffer(w25q_t *flash, uint32_t addr, uint8_t *buf, uint16_t len);
uint8_t W25Q_Sector_Erase(w25q_t *flash, uint32_t addr);
uint8_t W25Q_Chip_Erase(w25q_t *flash);
uint8_t W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len);
uint8_t W25Q_Write_MultiPage(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len);

#endif

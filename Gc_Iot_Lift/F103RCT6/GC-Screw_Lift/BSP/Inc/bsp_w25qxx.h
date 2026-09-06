#ifndef __BSP_W25QXX_H__
#define __BSP_W25QXX_H__

#include <stdint.h>

/* F103 兼容层：保留 W25Q API 名，底层走 I2C FRAM */

typedef struct w25q_dev
{
    void *unused;
} w25q_t;

#define W25Q_OK     0
#define W25Q_ERR    1

#ifndef W25Q_ERASE_BLOCK_SIZE
#define W25Q_ERASE_BLOCK_SIZE 256U
#endif

uint8_t  W25Q_Init_Device(w25q_t *flash);
uint32_t W25Q_Read_JEDEC_ID(w25q_t *flash);
uint16_t W25Q_Read_Device_ID(w25q_t *flash);
uint8_t  W25Q_Read_Buffer(w25q_t *flash, uint32_t addr, uint8_t *buf, uint16_t len);
uint8_t  W25Q_Sector_Erase(w25q_t *flash, uint32_t addr);
uint8_t  W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len);

#endif /* __BSP_W25QXX_H__ */

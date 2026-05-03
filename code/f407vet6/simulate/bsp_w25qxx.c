#include "bsp_w25qxx.h"
#include <string.h>

/* 用 1MB RAM 模拟 W25Q80 Flash */
static uint8_t flash_mem[1024 * 1024];

uint32_t W25Q_Read_JEDEC_ID(w25q_t *dev)
{
    (void)dev;
    return 0xEF4014;  /* W25Q80 */
}

uint8_t W25Q_Init_Device(w25q_t *dev)
{
    (void)dev;
    return W25Q_OK;
}

uint8_t W25Q_Read_Buffer(w25q_t *dev, uint32_t addr, uint8_t *buf, uint32_t size)
{
    (void)dev;
    if (addr + size > sizeof(flash_mem)) return W25Q_ERR;
    memcpy(buf, flash_mem + addr, size);
    return W25Q_OK;
}

uint8_t W25Q_Sector_Erase(w25q_t *dev, uint32_t addr)
{
    (void)dev;
    if (addr + 4096 > sizeof(flash_mem)) return W25Q_ERR;
    memset(flash_mem + addr, 0xFF, 4096);
    return W25Q_OK;
}

uint8_t W25Q_Page_Program(w25q_t *dev, uint32_t addr, const uint8_t *buf, uint32_t size)
{
    (void)dev;
    if (addr + size > sizeof(flash_mem)) return W25Q_ERR;
    /* 写之前先检查是否擦除过（bit只能1→0） */
    for (uint32_t i = 0; i < size; i++)
        flash_mem[addr + i] &= buf[i];
    return W25Q_OK;
}

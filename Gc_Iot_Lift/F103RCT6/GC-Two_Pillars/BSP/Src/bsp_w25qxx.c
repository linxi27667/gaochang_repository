#include "bsp_w25qxx.h"
#include "app_fram.h"
#include <stddef.h>
#include <string.h>

uint8_t W25Q_Init_Device(w25q_t *flash)
{
    (void)flash;
    return (App_Fram_Is_Ready() == 0U) ? W25Q_OK : W25Q_ERR;
}

uint32_t W25Q_Read_JEDEC_ID(w25q_t *flash)
{
    (void)flash;
    return 0x00464D24UL; /* 'FM$' */
}

uint16_t W25Q_Read_Device_ID(w25q_t *flash)
{
    (void)flash;
    return 0x2464U; /* FM24CL64 */
}

uint8_t W25Q_Read_Buffer(w25q_t *flash, uint32_t addr, uint8_t *buf, uint16_t len)
{
    (void)flash;
    if ((buf == NULL) || (len == 0U) || ((addr + len) > APP_FRAM_SIZE_BYTES))
    {
        return W25Q_ERR;
    }
    return (App_Fram_Read((uint16_t)addr, buf, len) == 0) ? W25Q_OK : W25Q_ERR;
}

uint8_t W25Q_Sector_Erase(w25q_t *flash, uint32_t addr)
{
    uint8_t ff[64];
    uint32_t left = W25Q_ERASE_BLOCK_SIZE;
    uint32_t cur = addr;

    (void)flash;
    if ((addr + W25Q_ERASE_BLOCK_SIZE) > APP_FRAM_SIZE_BYTES)
    {
        return W25Q_ERR;
    }

    memset(ff, 0xFF, sizeof(ff));
    while (left > 0U)
    {
        uint16_t chunk = (left > sizeof(ff)) ? (uint16_t)sizeof(ff) : (uint16_t)left;
        if (App_Fram_Write((uint16_t)cur, ff, chunk) != 0)
        {
            return W25Q_ERR;
        }
        cur += chunk;
        left -= chunk;
    }
    return W25Q_OK;
}

uint8_t W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len)
{
    (void)flash;
    if ((data == NULL) || (len == 0U) || ((addr + len) > APP_FRAM_SIZE_BYTES))
    {
        return W25Q_ERR;
    }
    return (App_Fram_Write((uint16_t)addr, data, len) == 0) ? W25Q_OK : W25Q_ERR;
}

uint8_t W25Q_Write_MultiPage(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len)
{
    return W25Q_Page_Program(flash, addr, data, len);
}

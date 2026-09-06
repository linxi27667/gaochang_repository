/**
 * @file bsp_spi.c
 * @brief SPI bus select/deselect/init helpers.
 */
#include "bsp_spi.h"

/* ================= SPI 总线基础操作 ================= */

// SPI 总线初始化
void SPI_Bus_Init(spi_bus_t *bus)
{
    if (bus && bus->Init)
    {
        bus->Init();
    }
}

// 选中 SPI 从设备（拉低 CS）
void SPI_Bus_Select(spi_bus_t *bus)
{
    bus->CS_Write(bus->cs.port, bus->cs.pin, 0);
}

// 取消选中 SPI 从设备（拉高 CS）
void SPI_Bus_Deselect(spi_bus_t *bus)
{
    bus->CS_Write(bus->cs.port, bus->cs.pin, 1);
}

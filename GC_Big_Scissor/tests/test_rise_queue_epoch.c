#include "app_maintenance.h"
#include "app_rise_queue.h"
#include "app_w25qxx.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FLASH_SIZE 0x00030000U

static uint8_t s_flash[FLASH_SIZE];
w25q_t W25Q_Flash;
w25q_stats_t g_stats;

uint8_t W25Q_Read_Buffer(w25q_t *flash, uint32_t addr, uint8_t *buf, uint16_t len)
{
    (void)flash;
    if ((addr + len) > FLASH_SIZE) return W25Q_ERR;
    memcpy(buf, &s_flash[addr], len);
    return W25Q_OK;
}

uint8_t W25Q_Sector_Erase(w25q_t *flash, uint32_t addr)
{
    (void)flash;
    memset(&s_flash[addr], 0xFF, W25Q_MAINTENANCE_SECTOR_SIZE);
    return W25Q_OK;
}

uint8_t W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t index;
    (void)flash;
    for (index = 0U; index < len; ++index) s_flash[addr + index] &= data[index];
    return W25Q_OK;
}

uint8_t W25Q_Write_MultiPage(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len)
{
    return W25Q_Page_Program(flash, addr, data, len);
}

int main(void)
{
    w25q_rise_event_t event;

    memset(s_flash, 0xFF, sizeof(s_flash));
    App_Maintenance_Init();
    App_RiseQueue_Init();

    App_RiseQueue_Enqueue(LIFT_ROLE_MAIN, 1U, 1U, 0U, 0U);
    assert(App_W25Qxx_RiseQueue_PersistPending() == W25Q_OK);

    /* Simulate reset_usage committing just before queue cleanup and a power loss. */
    assert(App_Maintenance_ResetUsage("reset-before-power-loss") == W25Q_OK);
    App_RiseQueue_Init();
    assert(App_W25Qxx_RiseQueue_Peek(&event) == 0U);
    assert(g_stats.up_count == 0U);

    puts("rise queue usage_epoch recovery test passed");
    return 0;
}

#include "app_maintenance.h"
#include "app_w25qxx.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FLASH_SIZE 0x00030000U
#define RECORD_SIZE 44U
#define RECORDS_PER_SECTOR (W25Q_MAINTENANCE_SECTOR_SIZE / RECORD_SIZE)

static uint8_t s_flash[FLASH_SIZE];
w25q_t W25Q_Flash;

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
    if ((addr % W25Q_MAINTENANCE_SECTOR_SIZE) != 0U) return W25Q_ERR;
    memset(&s_flash[addr], 0xFF, W25Q_MAINTENANCE_SECTOR_SIZE);
    return W25Q_OK;
}

uint8_t W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t index;
    (void)flash;
    if ((addr + len) > FLASH_SIZE) return W25Q_ERR;
    for (index = 0U; index < len; ++index) s_flash[addr + index] &= data[index];
    return W25Q_OK;
}

uint8_t W25Q_Write_MultiPage(w25q_t *flash, uint32_t addr, const uint8_t *data, uint16_t len)
{
    return W25Q_Page_Program(flash, addr, data, len);
}

static uint32_t ReadU32(uint32_t address)
{
    uint32_t value;
    memcpy(&value, &s_flash[address], sizeof(value));
    return value;
}

static uint32_t LatestRecordAddress(void)
{
    uint32_t sector;
    uint32_t slot;
    uint32_t newest_sequence = 0U;
    uint32_t newest_address = 0U;

    for (sector = 0U; sector < W25Q_MAINTENANCE_SECTOR_COUNT; ++sector)
    {
        for (slot = 0U; slot < RECORDS_PER_SECTOR; ++slot)
        {
            uint32_t address = W25Q_MAINTENANCE_ADDR +
                (sector * W25Q_MAINTENANCE_SECTOR_SIZE) + (slot * RECORD_SIZE);
            uint32_t sequence = ReadU32(address + 8U);
            if ((ReadU32(address) == 0x4D41494EU) && (sequence > newest_sequence))
            {
                newest_sequence = sequence;
                newest_address = address;
            }
        }
    }

    return newest_address;
}

int main(void)
{
    app_maintenance_status_t status;
    uint32_t latest;
    uint32_t index;

    memset(s_flash, 0xFF, sizeof(s_flash));
    App_Maintenance_Init();

    /* Main/sub are role telemetry only: each qualified device event counts once. */
    for (index = 0U; index < 3U; ++index) assert(App_Maintenance_RecordRise() == W25Q_OK);
    for (index = 0U; index < 2U; ++index) assert(App_Maintenance_RecordRise() == W25Q_OK);
    App_Maintenance_GetStatus(&status);
    assert(status.total_lift_count == 5U);
    assert(status.maintenance_lift_count == 5U);

    for (index = 5U; index < APP_MAINTENANCE_THRESHOLD; ++index)
        assert(App_Maintenance_RecordRise() == W25Q_OK);
    App_Maintenance_GetStatus(&status);
    assert(status.total_lift_count == APP_MAINTENANCE_THRESHOLD);
    assert(status.maintenance_due == 1U);

    /* Power-cycle recovery returns the newest complete record. */
    App_Maintenance_Init();
    App_Maintenance_GetStatus(&status);
    assert(status.total_lift_count == APP_MAINTENANCE_THRESHOLD);
    assert(status.maintenance_due == 1U);

    /* A damaged newest CRC is ignored and recovery falls back one record. */
    latest = LatestRecordAddress();
    assert(latest != 0U);
    s_flash[latest + 36U] ^= 0x01U;
    App_Maintenance_Init();
    App_Maintenance_GetStatus(&status);
    assert(status.total_lift_count == (APP_MAINTENANCE_THRESHOLD - 1U));
    assert(status.maintenance_due == 0U);

    assert(App_Maintenance_Done("maint-1") == W25Q_OK);
    App_Maintenance_GetStatus(&status);
    assert(status.maintenance_count == 1U);
    assert(status.maintenance_lift_count == 0U);
    assert(status.last_maintenance_total == (APP_MAINTENANCE_THRESHOLD - 1U));

    assert(App_Maintenance_Done("maint-1") == W25Q_OK);
    App_Maintenance_GetStatus(&status);
    assert(status.maintenance_count == 1U);

    assert(App_Maintenance_ResetUsage("reset-1") == W25Q_OK);
    App_Maintenance_Init();
    App_Maintenance_GetStatus(&status);
    assert(status.total_lift_count == 0U);
    assert(status.maintenance_lift_count == 0U);
    assert(status.maintenance_count == 0U);
    assert(status.maintenance_due == 0U);
    assert(status.usage_epoch == 1U);

    App_Maintenance_Init();
    assert(App_Maintenance_ResetUsage("reset-1") == W25Q_OK);
    App_Maintenance_GetStatus(&status);
    assert(status.usage_epoch == 1U);

    puts("maintenance ledger host tests passed");
    return 0;
}

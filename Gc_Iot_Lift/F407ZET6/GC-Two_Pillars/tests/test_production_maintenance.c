#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_w25qxx.h"

#define FLASH_SIZE 0x00024000U
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

uint8_t W25Q_Page_Program(w25q_t *flash, uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    (void)flash;
    if ((addr + len) > FLASH_SIZE) return W25Q_ERR;
    for (i = 0U; i < len; ++i) s_flash[addr + i] &= buf[i];
    return W25Q_OK;
}

uint8_t W25Q_Sector_Erase(w25q_t *flash, uint32_t addr)
{
    (void)flash;
    if ((addr % 0x1000U) != 0U || (addr + 0x1000U) > FLASH_SIZE) return W25Q_ERR;
    memset(&s_flash[addr], 0xFF, 0x1000U);
    return W25Q_OK;
}

#include "../APP/Src/app_rise_counter.c"

static void reset_runtime(void)
{
    memset(&s_maintenance_snapshot, 0, sizeof(s_maintenance_snapshot));
    s_maintenance_ledger_sequence = 0U;
    s_maintenance_next_record_slot = 0U;
    s_maintenance_command_count = 0U;
    s_maintenance_flush_needed = 0U;
}

int main(void)
{
    app_maintenance_snapshot_t saved;
    app_maintenance_ledger_record_v1_t legacy;
    uint32_t i;

    memset(s_flash, 0xFF, sizeof(s_flash));
    reset_runtime();
    memset(&legacy, 0, sizeof(legacy));
    legacy.magic = APP_MAINTENANCE_LEDGER_MAGIC;
    legacy.sequence = 7U;
    legacy.total_lift_count = 321U;
    legacy.maintenance_lift_count = 123U;
    legacy.usage_epoch = 4U;
    legacy.version = APP_MAINTENANCE_LEDGER_VERSION_V1;
    legacy.crc32 = MaintenanceLedger_Crc32((const uint8_t *)&legacy,
                                           APP_MAINTENANCE_LEDGER_V1_CRC_BYTES);
    legacy.commit = APP_MAINTENANCE_LEDGER_COMMIT;
    memcpy(&s_flash[W25Q_MAINTENANCE_LEDGER_ADDR], &legacy, sizeof(legacy));
    assert(MaintenanceLedger_Load(0U) == 1U);
    MaintenanceLedger_CopySnapshotNoLock(&saved);
    assert(saved.total_lift_count == 321U);
    assert(saved.maintenance_lift_count == 123U);
    assert(saved.usage_epoch == 4U);

    memset(s_flash, 0xFF, sizeof(s_flash));
    reset_runtime();
    legacy.commit = 0xFFFFFFFFU;
    memcpy(&s_flash[W25Q_MAINTENANCE_LEDGER_ADDR], &legacy, sizeof(legacy));
    assert(MaintenanceLedger_Load(0U) == 1U);
    MaintenanceLedger_CopySnapshotNoLock(&saved);
    assert(saved.total_lift_count == 0U);

    s_maintenance_snapshot.total_lift_count = 4999U;
    s_maintenance_snapshot.maintenance_lift_count = 4999U;
    assert(MaintenanceLedger_Append(&s_maintenance_snapshot) == 1U);
    s_maintenance_snapshot.total_lift_count++;
    s_maintenance_snapshot.maintenance_lift_count++;
    s_maintenance_snapshot.maintenance_due = 1U;
    assert(MaintenanceLedger_Append(&s_maintenance_snapshot) == 1U);

    s_maintenance_snapshot.maintenance_count = 1U;
    s_maintenance_snapshot.last_maintenance_total = 5000U;
    s_maintenance_snapshot.maintenance_lift_count = 0U;
    s_maintenance_snapshot.maintenance_due = 0U;
    s_maintenance_snapshot.last_command_type = APP_MAINTENANCE_COMMAND_DONE;
    s_maintenance_snapshot.last_command_hash = MaintenanceLedger_CommandHash(APP_MAINTENANCE_COMMAND_DONE, "maint-1");
    assert(MaintenanceLedger_Append(&s_maintenance_snapshot) == 1U);

    reset_runtime();
    assert(MaintenanceLedger_Load(0U) == 1U);
    MaintenanceLedger_CopySnapshotNoLock(&saved);
    assert(saved.total_lift_count == 5000U);
    assert(saved.maintenance_count == 1U);
    assert(MaintenanceLedger_CommandSeen(APP_MAINTENANCE_COMMAND_DONE,
        MaintenanceLedger_CommandHash(APP_MAINTENANCE_COMMAND_DONE, "maint-1")) == 1U);

    for (i = 0U; i < 300U; ++i)
    {
        s_maintenance_snapshot.total_lift_count++;
        s_maintenance_snapshot.maintenance_lift_count++;
        assert(MaintenanceLedger_Append(&s_maintenance_snapshot) == 1U);
    }
    reset_runtime();
    assert(MaintenanceLedger_Load(0U) == 1U);
    MaintenanceLedger_CopySnapshotNoLock(&saved);
    assert(saved.total_lift_count == 5300U);

    s_flash[MaintenanceLedger_RecordAddress((s_maintenance_next_record_slot + APP_MAINTENANCE_LEDGER_TOTAL_RECORDS - 1U) % APP_MAINTENANCE_LEDGER_TOTAL_RECORDS) + 8U] ^= 0x01U;
    reset_runtime();
    assert(MaintenanceLedger_Load(0U) == 1U);
    MaintenanceLedger_CopySnapshotNoLock(&saved);
    assert(saved.total_lift_count == 5299U);

    puts("double-post production maintenance tests passed");
    return 0;
}

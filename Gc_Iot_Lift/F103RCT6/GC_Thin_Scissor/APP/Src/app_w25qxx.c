#include "app_w25qxx.h"
#include "app_product.h"

#include <stddef.h>
#include <string.h>

#if W25Q_DEBUG == 1
#include "elog.h"
#endif

#define W25Q_RISE_STATS_JOURNAL_MAGIC   0x52535453U
#define W25Q_RISE_STATS_JOURNAL_VERSION 1U
#define W25Q_RISE_STATS_JOURNAL_COMMIT  0x00000000U
#define W25Q_MAINTENANCE_JOURNAL_MAGIC   0x4D41494EU
#define W25Q_MAINTENANCE_JOURNAL_VERSION 1U
#define W25Q_MAINTENANCE_JOURNAL_COMMIT  0x00000000U

/* The commit word is programmed last and is intentionally outside the CRC. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t remainder_ms;
    uint32_t sequence;
    uint32_t total_up_count;
    uint32_t pending_count;
    uint32_t next_sequence;
    uint32_t crc32;
    uint32_t commit;
} w25q_rise_stats_record_t;

/* The commit word is programmed after the CRC-covered body. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t total_lift_count;
    uint32_t maintenance_lift_count;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint32_t maintenance_due;
    uint32_t usage_epoch;
    uint32_t crc32;
    uint32_t commit;
} w25q_maintenance_record_t;

#define W25Q_RISE_STATS_RECORDS_PER_SECTOR \
    (W25Q_RISE_STATS_JOURNAL_SECTOR_SIZE / sizeof(w25q_rise_stats_record_t))
#define W25Q_MAINTENANCE_RECORDS_PER_SECTOR \
    (W25Q_MAINTENANCE_JOURNAL_SECTOR_SIZE / sizeof(w25q_maintenance_record_t))

w25q_storage_t g_w25q_storage = {0};

w25q_config_t g_config = {
    .header                    = W25Q_CONFIG_HEADER,
    .version                   = W25Q_CONFIG_VERSION,
    .product_type              = PRODUCT_TYPE_THIN_SCISSOR,
    .reserved1                 = 0,
    .motor_to_valve_delay_ms   = 200,
    .motor_hold_ms             = 2500,
    .sub_motor_hold_ms         = 1500,
    .module_enable_mask        = 0,
    .reserved2                 = 0,
    .photoelectric_debounce_ms = 50,
    .estop_debounce_ms         = 20,
    .tolerance_up              = 4,
    .tolerance_down            = 4,
    .stall_timeout_ms          = 2000,
    .balance_wait_max_ms       = 10000,
    .collision_debounce_ms     = 50,
    .secondary_descent_pulses  = 30,
    .dual_column_mode          = 1,
    .screw_lead_mm             = 8,
    .max_pulses                = MAX_PULSES,
};

w25q_stats_t g_stats = {0};

w25q_rise_stats_t g_rise_stats = {
    .next_sequence = 1U,
};

w25q_maintenance_t g_maintenance = {0};

w25q_t W25Q_Flash = {
    .unused = 0
};

static uint8_t s_rise_stats_scanned = 0U;
static uint32_t s_rise_journal_next_addr = W25Q_RISE_STATS_JOURNAL_ADDR;
static uint32_t s_rise_journal_sequence = 0U;
static uint8_t s_maintenance_scanned = 0U;
static uint8_t s_maintenance_found = 0U;
static uint32_t s_maintenance_journal_next_addr = W25Q_MAINTENANCE_JOURNAL_ADDR;
static uint32_t s_maintenance_journal_sequence = 0U;
static uint32_t s_maintenance_command_hash = 0U;
static uint8_t s_config_active_slot = 0U;
static uint8_t s_stats_active_slot = 0U;
static uint16_t s_oplog_write_index = 0U;
static uint16_t s_oplog_count = 0U;
static uint8_t s_oplog_scanned = 0U;

static uint32_t App_W25Qxx_Crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t bit;

    while (len-- != 0U)
    {
        crc ^= *data++;
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
        }
    }

    return ~crc;
}

static uint16_t App_W25Qxx_Crc16(const uint8_t *data, uint32_t length)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t bit;

    for (i = 0U; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001U);
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static uint32_t storage_crc(const w25q_storage_t *s)
{
    return s->magic ^ s->debug_counter;
}

static uint8_t storage_validate(const w25q_storage_t *s)
{
    return ((s->magic == W25Q_STORAGE_MAGIC) && (s->crc == storage_crc(s))) ? 1U : 0U;
}

static uint8_t storage_read_slot(w25q_storage_t *out, uint32_t addr)
{
    if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, (uint16_t)W25Q_STORAGE_SIZE) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    return (storage_validate(out) != 0U) ? W25Q_OK : W25Q_ERR;
}

static uint8_t config_validate(const w25q_config_t *cfg)
{
    w25q_config_t tmp;

    if ((cfg->header != W25Q_CONFIG_HEADER) || (cfg->version != W25Q_CONFIG_VERSION))
    {
        return 0U;
    }
    tmp = *cfg;
    tmp.crc16 = 0U;
    return (cfg->crc16 == App_W25Qxx_Crc16((const uint8_t *)&tmp,
                                            (uint32_t)sizeof(tmp))) ? 1U : 0U;
}

static uint8_t config_read_slot(uint32_t addr, w25q_config_t *out)
{
    if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, (uint16_t)sizeof(*out)) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    return (config_validate(out) != 0U) ? W25Q_OK : W25Q_ERR;
}

static uint8_t stats_validate(const w25q_stats_t *stats)
{
    uint32_t expect;

    if ((stats->magic != W25Q_STATS_MAGIC) || (stats->version != W25Q_STATS_VERSION))
    {
        return 0U;
    }
    expect = App_W25Qxx_Crc32((const uint8_t *)stats,
                              (uint32_t)offsetof(w25q_stats_t, crc));
    return (stats->crc == expect) ? 1U : 0U;
}

static uint8_t stats_read_slot(uint32_t addr, w25q_stats_t *out)
{
    if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, (uint16_t)sizeof(*out)) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    return (stats_validate(out) != 0U) ? W25Q_OK : W25Q_ERR;
}

static uint32_t stats_activity(const w25q_stats_t *stats)
{
    return stats->boot_count + stats->up_count + stats->down_count +
           stats->lock_count + stats->refill_count + stats->estop_count +
           stats->photo_alarm_count + stats->total_run_ms;
}

static uint8_t oplog_entry_is_erased(const w25q_op_log_entry_t *entry)
{
    const uint8_t *bytes = (const uint8_t *)entry;
    uint32_t i;

    for (i = 0U; i < sizeof(*entry); i++)
    {
        if (bytes[i] != 0xFFU)
        {
            return 0U;
        }
    }
    return 1U;
}

static void oplog_scan(void)
{
    uint16_t index;
    w25q_op_log_entry_t entry;

    s_oplog_write_index = 0U;
    s_oplog_count = 0U;
    for (index = 0U; index < W25Q_OPLOG_MAX_ENTRIES; index++)
    {
        uint32_t addr = W25Q_OPLOG_ADDR + ((uint32_t)index * W25Q_OPLOG_ENTRY_SIZE);
        if (W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)&entry,
                             (uint16_t)sizeof(entry)) != W25Q_OK)
        {
            break;
        }
        if (oplog_entry_is_erased(&entry) != 0U)
        {
            s_oplog_write_index = index;
            break;
        }
        s_oplog_count = (uint16_t)(index + 1U);
        s_oplog_write_index = (uint16_t)((index + 1U) % W25Q_OPLOG_MAX_ENTRIES);
    }
    if (s_oplog_count > W25Q_OPLOG_MAX_ENTRIES)
    {
        s_oplog_count = W25Q_OPLOG_MAX_ENTRIES;
    }
    s_oplog_scanned = 1U;
}

static uint32_t App_W25Qxx_RiseStats_JournalEnd(void)
{
    return W25Q_RISE_STATS_JOURNAL_ADDR +
           (W25Q_RISE_STATS_JOURNAL_SECTOR_SIZE * W25Q_RISE_STATS_JOURNAL_SECTOR_COUNT);
}

static uint32_t App_W25Qxx_RiseStats_AdvanceAddress(uint32_t address)
{
    address += sizeof(w25q_rise_stats_record_t);
    if (address >= App_W25Qxx_RiseStats_JournalEnd())
    {
        address = W25Q_RISE_STATS_JOURNAL_ADDR;
    }
    return address;
}

static uint8_t App_W25Qxx_RiseStats_IsSectorStart(uint32_t address)
{
    return (((address - W25Q_RISE_STATS_JOURNAL_ADDR) %
             W25Q_RISE_STATS_JOURNAL_SECTOR_SIZE) == 0U) ? 1U : 0U;
}

static uint8_t App_W25Qxx_RiseStats_IsRecordErased(const w25q_rise_stats_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t index;

    for (index = 0U; index < sizeof(*record); ++index)
    {
        if (bytes[index] != 0xFFU)
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t App_W25Qxx_RiseStats_IsRecordValid(const w25q_rise_stats_record_t *record)
{
    if ((record->magic != W25Q_RISE_STATS_JOURNAL_MAGIC) ||
        (record->version != W25Q_RISE_STATS_JOURNAL_VERSION) ||
        (record->sequence == 0U) ||
        (record->remainder_ms >= W25Q_RISE_STATS_REMAINDER_LIMIT_MS) ||
        (record->commit != W25Q_RISE_STATS_JOURNAL_COMMIT))
    {
        return 0U;
    }

    return (record->crc32 == App_W25Qxx_Crc32((const uint8_t *)record,
                                                offsetof(w25q_rise_stats_record_t, crc32))) ? 1U : 0U;
}

static uint8_t App_W25Qxx_RiseStats_IsSequenceNewer(uint32_t candidate, uint32_t reference)
{
    return ((int32_t)(candidate - reference) > 0) ? 1U : 0U;
}

static void App_W25Qxx_RiseStats_SyncLegacyUpCount(void)
{
    g_stats.up_count = g_rise_stats.total_up_count;

    /* A thin scissor has only the main lift role. */
    if (g_product_type == PRODUCT_TYPE_THIN_SCISSOR)
    {
        g_stats.up_count_main = g_rise_stats.total_up_count;
        g_stats.up_count_sub = 0U;
    }
}

static void App_W25Qxx_RiseStats_SetDefaults(void)
{
    g_rise_stats.total_up_count = 0U;
    g_rise_stats.remainder_ms = 0U;
    g_rise_stats.reserved = 0U;
    g_rise_stats.pending_count = 0U;
    g_rise_stats.next_sequence = 1U;
    App_W25Qxx_RiseStats_SyncLegacyUpCount();
}

static uint8_t App_W25Qxx_RiseStats_ReadRecord(uint32_t address,
                                                 w25q_rise_stats_record_t *record)
{
    return W25Q_Read_Buffer(&W25Q_Flash, address, (uint8_t *)record, sizeof(*record));
}

/* Power-loss fragments are skipped.  A sector is erased only when it becomes
 * the next ring sector, so recent records remain available for recovery. */
static uint8_t App_W25Qxx_RiseStats_PrepareWriteAddress(uint32_t *address)
{
    uint32_t attempts;
    w25q_rise_stats_record_t record;

    for (attempts = 0U;
         attempts < (W25Q_RISE_STATS_RECORDS_PER_SECTOR * W25Q_RISE_STATS_JOURNAL_SECTOR_COUNT);
         ++attempts)
    {
        if (App_W25Qxx_RiseStats_ReadRecord(*address, &record) != W25Q_OK)
        {
            return W25Q_ERR;
        }

        if (App_W25Qxx_RiseStats_IsRecordErased(&record) != 0U)
        {
            return W25Q_OK;
        }

        if (App_W25Qxx_RiseStats_IsSectorStart(*address) != 0U)
        {
            if (W25Q_Sector_Erase(&W25Q_Flash, *address) != W25Q_OK)
            {
                return W25Q_ERR;
            }
            return W25Q_OK;
        }

        *address = App_W25Qxx_RiseStats_AdvanceAddress(*address);
    }

    return W25Q_ERR;
}

static uint8_t App_W25Qxx_RiseStats_WriteRecord(uint32_t address,
                                                  const w25q_rise_stats_record_t *record)
{
    uint32_t commit = W25Q_RISE_STATS_JOURNAL_COMMIT;
    w25q_rise_stats_record_t verify;

    if (W25Q_Write_MultiPage(&W25Q_Flash, address, (const uint8_t *)record,
                             offsetof(w25q_rise_stats_record_t, commit)) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    if (W25Q_Write_MultiPage(&W25Q_Flash,
                             address + offsetof(w25q_rise_stats_record_t, commit),
                             (const uint8_t *)&commit, sizeof(commit)) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    if (App_W25Qxx_RiseStats_ReadRecord(address, &verify) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    if ((App_W25Qxx_RiseStats_IsRecordValid(&verify) == 0U) ||
        (verify.sequence != record->sequence))
    {
        return W25Q_ERR;
    }

    return W25Q_OK;
}

static uint32_t App_W25Qxx_Maintenance_JournalEnd(void)
{
    return W25Q_MAINTENANCE_JOURNAL_ADDR +
           (W25Q_MAINTENANCE_JOURNAL_SECTOR_SIZE * W25Q_MAINTENANCE_JOURNAL_SECTOR_COUNT);
}

static uint32_t App_W25Qxx_Maintenance_AdvanceAddress(uint32_t address)
{
    address += sizeof(w25q_maintenance_record_t);
    return (address >= App_W25Qxx_Maintenance_JournalEnd()) ?
           W25Q_MAINTENANCE_JOURNAL_ADDR : address;
}

static uint8_t App_W25Qxx_Maintenance_IsSectorStart(uint32_t address)
{
    return (((address - W25Q_MAINTENANCE_JOURNAL_ADDR) %
             W25Q_MAINTENANCE_JOURNAL_SECTOR_SIZE) == 0U) ? 1U : 0U;
}

static uint8_t App_W25Qxx_Maintenance_IsRecordErased(const w25q_maintenance_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t index;

    for (index = 0U; index < sizeof(*record); ++index)
    {
        if (bytes[index] != 0xFFU)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t App_W25Qxx_Maintenance_IsRecordValid(const w25q_maintenance_record_t *record)
{
    if ((record->magic != W25Q_MAINTENANCE_JOURNAL_MAGIC) ||
        (record->version != W25Q_MAINTENANCE_JOURNAL_VERSION) ||
        (record->sequence == 0U) ||
        (record->commit != W25Q_MAINTENANCE_JOURNAL_COMMIT))
    {
        return 0U;
    }

    return (record->crc32 == App_W25Qxx_Crc32((const uint8_t *)record,
                                                offsetof(w25q_maintenance_record_t, crc32))) ? 1U : 0U;
}

static uint8_t App_W25Qxx_Maintenance_PrepareWriteAddress(uint32_t *address)
{
    uint32_t attempts;
    w25q_maintenance_record_t record;

    for (attempts = 0U;
         attempts < (W25Q_MAINTENANCE_RECORDS_PER_SECTOR * W25Q_MAINTENANCE_JOURNAL_SECTOR_COUNT);
         ++attempts)
    {
        if (W25Q_Read_Buffer(&W25Q_Flash, *address, (uint8_t *)&record, sizeof(record)) != W25Q_OK)
        {
            return W25Q_ERR;
        }
        if (App_W25Qxx_Maintenance_IsRecordErased(&record) != 0U)
        {
            return W25Q_OK;
        }
        if (App_W25Qxx_Maintenance_IsSectorStart(*address) != 0U)
        {
            return W25Q_Sector_Erase(&W25Q_Flash, *address);
        }
        *address = App_W25Qxx_Maintenance_AdvanceAddress(*address);
    }

    return W25Q_ERR;
}

static uint8_t App_W25Qxx_Maintenance_WriteRecord(uint32_t address,
                                                    const w25q_maintenance_record_t *record)
{
    uint32_t commit = W25Q_MAINTENANCE_JOURNAL_COMMIT;
    w25q_maintenance_record_t verify;

    if (W25Q_Write_MultiPage(&W25Q_Flash, address, (const uint8_t *)record,
                             offsetof(w25q_maintenance_record_t, commit)) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    if (W25Q_Write_MultiPage(&W25Q_Flash,
                             address + offsetof(w25q_maintenance_record_t, commit),
                             (const uint8_t *)&commit, sizeof(commit)) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    if (W25Q_Read_Buffer(&W25Q_Flash, address, (uint8_t *)&verify, sizeof(verify)) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    return ((App_W25Qxx_Maintenance_IsRecordValid(&verify) != 0U) &&
            (verify.sequence == record->sequence)) ? W25Q_OK : W25Q_ERR;
}

static void App_W25Qxx_Maintenance_SetDefaults(void)
{
    memset(&g_maintenance, 0, sizeof(g_maintenance));
    s_maintenance_command_hash = 0U;
}

static uint32_t App_W25Qxx_Maintenance_CommandHash(uint8_t operation, const char *msg_id)
{
    uint32_t hash = 2166136261U ^ operation;
    if ((msg_id == NULL) || (msg_id[0] == '\0')) return 0U;
    while (*msg_id != '\0') {
        hash ^= (uint8_t)*msg_id++;
        hash *= 16777619U;
    }
    hash &= 0x7FFFFFFFU;
    return (hash == 0U) ? 1U : hash;
}

void App_W25Qxx_System_Init(void)
{
    uint8_t ret;
    uint8_t load_ret;
    uint8_t maintenance_ret;

    ret = W25Q_Init_Device(&W25Q_Flash);

    if (ret != W25Q_OK)
    {
        #if W25Q_DEBUG == 1
        elog_e("W25Q", "[FRAM] chip init FAILED (SPI comm error)");
        #endif
        return;
    }

    App_W25Qxx_Storage_Load();
    App_W25Qxx_Config_Load();
    App_W25Qxx_Stats_Load();
    oplog_scan();

    load_ret = App_W25Qxx_RiseStats_Load();
    maintenance_ret = App_W25Qxx_Maintenance_Load();

    if ((load_ret == W25Q_OK) && (maintenance_ret == W25Q_OK))
    {
        if (s_maintenance_found == 0U)
        {
            if (g_rise_stats.total_up_count != 0U)
            {
                g_maintenance.total_lift_count = g_rise_stats.total_up_count;
                g_maintenance.maintenance_lift_count = g_rise_stats.total_up_count;
                g_maintenance.maintenance_due =
                    (g_maintenance.maintenance_lift_count >= W25Q_MAINTENANCE_THRESHOLD) ? 1U : 0U;
                (void)App_W25Qxx_Maintenance_Save();
            }
        }
        else if (g_maintenance.total_lift_count > g_rise_stats.total_up_count)
        {
            uint32_t difference = g_maintenance.total_lift_count - g_rise_stats.total_up_count;
            g_rise_stats.total_up_count = g_maintenance.total_lift_count;
            if ((UINT32_MAX - g_rise_stats.pending_count) < difference)
            {
                g_rise_stats.pending_count = UINT32_MAX;
            }
            else
            {
                g_rise_stats.pending_count += difference;
            }
            (void)App_W25Qxx_RiseStats_Save();
        }
        else if (g_maintenance.total_lift_count < g_rise_stats.total_up_count)
        {
            g_rise_stats.total_up_count = g_maintenance.total_lift_count;
            g_rise_stats.pending_count = 0U;
            g_rise_stats.remainder_ms = 0U;
            (void)App_W25Qxx_RiseStats_Save();
        }
    }

    #if W25Q_DEBUG == 1
    elog_i("W25Q", "[FRAM] JEDEC=0x%06lX DEV=0x%04X",
           W25Q_Read_JEDEC_ID(&W25Q_Flash),
           W25Q_Read_Device_ID(&W25Q_Flash));
    if (load_ret == W25Q_OK)
    {
        elog_i("W25Q", "[FRAM] rise total=%lu pending=%lu remainder=%u",
               (unsigned long)g_rise_stats.total_up_count,
               (unsigned long)g_rise_stats.pending_count,
               (unsigned int)g_rise_stats.remainder_ms);
    }
    else
    {
        elog_e("W25Q", "[FRAM] rise stats load FAILED");
    }
    if (maintenance_ret == W25Q_OK)
    {
        elog_i("W25Q", "[FRAM] maintenance total=%lu cycle=%lu due=%u epoch=%lu",
               (unsigned long)g_maintenance.total_lift_count,
               (unsigned long)g_maintenance.maintenance_lift_count,
               (unsigned int)g_maintenance.maintenance_due,
               (unsigned long)g_maintenance.usage_epoch);
    }
    #else
    (void)load_ret;
    (void)maintenance_ret;
    #endif
}

uint32_t App_W25Qxx_Get_JEDEC_ID(void)
{
    return W25Q_Read_JEDEC_ID(&W25Q_Flash);
}

uint8_t App_W25Qxx_RiseStats_Load(void)
{
    uint32_t sector;
    uint32_t slot;
    uint32_t address;
    uint8_t found = 0U;
    uint32_t newest_sequence = 0U;
    uint32_t newest_address = W25Q_RISE_STATS_JOURNAL_ADDR;
    w25q_rise_stats_record_t record;
    w25q_rise_stats_record_t newest_record;

    if ((sizeof(w25q_rise_stats_record_t) != 32U) ||
        (W25Q_RISE_STATS_RECORDS_PER_SECTOR == 0U))
    {
        return W25Q_ERR;
    }

    s_rise_stats_scanned = 0U;
    for (sector = 0U; sector < W25Q_RISE_STATS_JOURNAL_SECTOR_COUNT; ++sector)
    {
        for (slot = 0U; slot < W25Q_RISE_STATS_RECORDS_PER_SECTOR; ++slot)
        {
            address = W25Q_RISE_STATS_JOURNAL_ADDR +
                      (sector * W25Q_RISE_STATS_JOURNAL_SECTOR_SIZE) +
                      (slot * sizeof(w25q_rise_stats_record_t));
            if (App_W25Qxx_RiseStats_ReadRecord(address, &record) != W25Q_OK)
            {
                return W25Q_ERR;
            }

            if ((App_W25Qxx_RiseStats_IsRecordValid(&record) != 0U) &&
                ((found == 0U) ||
                 (App_W25Qxx_RiseStats_IsSequenceNewer(record.sequence, newest_sequence) != 0U)))
            {
                newest_record = record;
                newest_sequence = record.sequence;
                newest_address = address;
                found = 1U;
            }
        }
    }

    if (found == 0U)
    {
        App_W25Qxx_RiseStats_SetDefaults();
        s_rise_journal_sequence = 0U;
        s_rise_journal_next_addr = W25Q_RISE_STATS_JOURNAL_ADDR;
    }
    else
    {
        g_rise_stats.total_up_count = newest_record.total_up_count;
        g_rise_stats.remainder_ms = newest_record.remainder_ms;
        g_rise_stats.reserved = 0U;
        g_rise_stats.pending_count = newest_record.pending_count;
        g_rise_stats.next_sequence = newest_record.next_sequence;
        App_W25Qxx_RiseStats_SyncLegacyUpCount();

        s_rise_journal_sequence = newest_sequence;
        s_rise_journal_next_addr = App_W25Qxx_RiseStats_AdvanceAddress(newest_address);
    }

    s_rise_stats_scanned = 1U;
    return W25Q_OK;
}

uint8_t App_W25Qxx_RiseStats_Save(void)
{
    uint32_t write_address;
    uint32_t sequence;
    w25q_rise_stats_record_t record;

    if (g_rise_stats.remainder_ms >= W25Q_RISE_STATS_REMAINDER_LIMIT_MS)
    {
        return W25Q_ERR;
    }

    if (s_rise_stats_scanned == 0U)
    {
        if (App_W25Qxx_RiseStats_Load() != W25Q_OK)
        {
            return W25Q_ERR;
        }
    }

    write_address = s_rise_journal_next_addr;
    if (App_W25Qxx_RiseStats_PrepareWriteAddress(&write_address) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    sequence = s_rise_journal_sequence + 1U;
    if (sequence == 0U)
    {
        sequence = 1U;
    }

    record.magic = W25Q_RISE_STATS_JOURNAL_MAGIC;
    record.version = W25Q_RISE_STATS_JOURNAL_VERSION;
    record.remainder_ms = g_rise_stats.remainder_ms;
    record.sequence = sequence;
    record.total_up_count = g_rise_stats.total_up_count;
    record.pending_count = g_rise_stats.pending_count;
    record.next_sequence = g_rise_stats.next_sequence;
    record.crc32 = App_W25Qxx_Crc32((const uint8_t *)&record,
                                    offsetof(w25q_rise_stats_record_t, crc32));
    record.commit = 0xFFFFFFFFU;

    if (App_W25Qxx_RiseStats_WriteRecord(write_address, &record) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    s_rise_journal_sequence = sequence;
    s_rise_journal_next_addr = App_W25Qxx_RiseStats_AdvanceAddress(write_address);
    App_W25Qxx_RiseStats_SyncLegacyUpCount();
    return W25Q_OK;
}

uint8_t App_W25Qxx_Maintenance_Load(void)
{
    uint32_t sector;
    uint32_t slot;
    uint32_t address;
    uint32_t newest_sequence = 0U;
    uint32_t newest_address = W25Q_MAINTENANCE_JOURNAL_ADDR;
    uint8_t found = 0U;
    w25q_maintenance_record_t record;
    w25q_maintenance_record_t newest_record;

    if ((sizeof(w25q_maintenance_record_t) != 44U) ||
        (W25Q_MAINTENANCE_RECORDS_PER_SECTOR == 0U))
    {
        return W25Q_ERR;
    }

    s_maintenance_scanned = 0U;
    s_maintenance_found = 0U;
    for (sector = 0U; sector < W25Q_MAINTENANCE_JOURNAL_SECTOR_COUNT; ++sector)
    {
        for (slot = 0U; slot < W25Q_MAINTENANCE_RECORDS_PER_SECTOR; ++slot)
        {
            address = W25Q_MAINTENANCE_JOURNAL_ADDR +
                      (sector * W25Q_MAINTENANCE_JOURNAL_SECTOR_SIZE) +
                      (slot * sizeof(w25q_maintenance_record_t));
            if (W25Q_Read_Buffer(&W25Q_Flash, address, (uint8_t *)&record, sizeof(record)) != W25Q_OK)
            {
                return W25Q_ERR;
            }
            if ((App_W25Qxx_Maintenance_IsRecordValid(&record) != 0U) &&
                ((found == 0U) ||
                 (App_W25Qxx_RiseStats_IsSequenceNewer(record.sequence, newest_sequence) != 0U)))
            {
                newest_record = record;
                newest_sequence = record.sequence;
                newest_address = address;
                found = 1U;
            }
        }
    }

    if (found == 0U)
    {
        App_W25Qxx_Maintenance_SetDefaults();
        s_maintenance_journal_sequence = 0U;
        s_maintenance_journal_next_addr = W25Q_MAINTENANCE_JOURNAL_ADDR;
    }
    else
    {
        g_maintenance.total_lift_count = newest_record.total_lift_count;
        g_maintenance.maintenance_lift_count = newest_record.maintenance_lift_count;
        g_maintenance.maintenance_count = newest_record.maintenance_count;
        g_maintenance.last_maintenance_total = newest_record.last_maintenance_total;
        g_maintenance.maintenance_due = (uint8_t)(newest_record.maintenance_due & 1U);
        s_maintenance_command_hash = newest_record.maintenance_due >> 1U;
        g_maintenance.reserved[0] = 0U;
        g_maintenance.reserved[1] = 0U;
        g_maintenance.reserved[2] = 0U;
        g_maintenance.usage_epoch = newest_record.usage_epoch;
        s_maintenance_journal_sequence = newest_sequence;
        s_maintenance_journal_next_addr = App_W25Qxx_Maintenance_AdvanceAddress(newest_address);
        s_maintenance_found = 1U;
    }

    s_maintenance_scanned = 1U;
    return W25Q_OK;
}

uint8_t App_W25Qxx_Maintenance_Save(void)
{
    uint32_t write_address;
    uint32_t sequence;
    w25q_maintenance_record_t record;

    if (s_maintenance_scanned == 0U)
    {
        if (App_W25Qxx_Maintenance_Load() != W25Q_OK)
        {
            return W25Q_ERR;
        }
    }

    write_address = s_maintenance_journal_next_addr;
    if (App_W25Qxx_Maintenance_PrepareWriteAddress(&write_address) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    sequence = s_maintenance_journal_sequence + 1U;
    if (sequence == 0U)
    {
        sequence = 1U;
    }

    record.magic = W25Q_MAINTENANCE_JOURNAL_MAGIC;
    record.version = W25Q_MAINTENANCE_JOURNAL_VERSION;
    record.reserved = 0U;
    record.sequence = sequence;
    record.total_lift_count = g_maintenance.total_lift_count;
    record.maintenance_lift_count = g_maintenance.maintenance_lift_count;
    record.maintenance_count = g_maintenance.maintenance_count;
    record.last_maintenance_total = g_maintenance.last_maintenance_total;
    /* Bit 0 keeps the legacy due flag; bits 1..31 persist command idempotency. */
    record.maintenance_due = (g_maintenance.maintenance_due ? 1U : 0U) |
                             (s_maintenance_command_hash << 1U);
    record.usage_epoch = g_maintenance.usage_epoch;
    record.crc32 = App_W25Qxx_Crc32((const uint8_t *)&record,
                                    offsetof(w25q_maintenance_record_t, crc32));
    record.commit = 0xFFFFFFFFU;

    if (App_W25Qxx_Maintenance_WriteRecord(write_address, &record) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    s_maintenance_journal_sequence = sequence;
    s_maintenance_journal_next_addr = App_W25Qxx_Maintenance_AdvanceAddress(write_address);
    s_maintenance_found = 1U;
    return W25Q_OK;
}

void App_W25Qxx_Maintenance_AddLiftUnits(uint32_t units)
{
    if (units == 0U)
    {
        return;
    }

    g_maintenance.total_lift_count = ((UINT32_MAX - g_maintenance.total_lift_count) < units) ?
                                     UINT32_MAX : (g_maintenance.total_lift_count + units);
    g_maintenance.maintenance_lift_count =
        ((UINT32_MAX - g_maintenance.maintenance_lift_count) < units) ?
        UINT32_MAX : (g_maintenance.maintenance_lift_count + units);
    if (g_maintenance.maintenance_lift_count >= W25Q_MAINTENANCE_THRESHOLD)
    {
        g_maintenance.maintenance_due = 1U;
    }
}

uint8_t App_W25Qxx_Maintenance_Done(const char *msg_id, w25q_maintenance_t *saved)
{
    w25q_maintenance_t previous;
    uint32_t previous_hash;
    uint32_t hash = App_W25Qxx_Maintenance_CommandHash(1U, msg_id);

    if ((hash == 0U) || (saved == NULL)) return W25Q_ERR;
    if ((s_maintenance_scanned == 0U) && (App_W25Qxx_Maintenance_Load() != W25Q_OK)) return W25Q_ERR;
    if (s_maintenance_command_hash == hash) { *saved = g_maintenance; return W25Q_OK; }

    previous = g_maintenance;
    previous_hash = s_maintenance_command_hash;
    if (g_maintenance.maintenance_count != UINT32_MAX) g_maintenance.maintenance_count++;
    g_maintenance.last_maintenance_total = g_maintenance.total_lift_count;
    g_maintenance.maintenance_lift_count = 0U;
    g_maintenance.maintenance_due = 0U;
    s_maintenance_command_hash = hash;
    if (App_W25Qxx_Maintenance_Save() != W25Q_OK) {
        g_maintenance = previous;
        s_maintenance_command_hash = previous_hash;
        return W25Q_ERR;
    }
    *saved = g_maintenance;
    return W25Q_OK;
}

uint8_t App_W25Qxx_Maintenance_ResetUsage(const char *msg_id, w25q_maintenance_t *saved)
{
    w25q_maintenance_t previous;
    uint32_t previous_hash;
    uint32_t next_epoch;
    uint32_t hash = App_W25Qxx_Maintenance_CommandHash(2U, msg_id);

    if ((hash == 0U) || (saved == NULL)) return W25Q_ERR;
    if ((s_maintenance_scanned == 0U) && (App_W25Qxx_Maintenance_Load() != W25Q_OK)) return W25Q_ERR;
    if (s_maintenance_command_hash == hash) { *saved = g_maintenance; return W25Q_OK; }

    previous = g_maintenance;
    previous_hash = s_maintenance_command_hash;
    next_epoch = g_maintenance.usage_epoch + 1U;
    memset(&g_maintenance, 0, sizeof(g_maintenance));
    g_maintenance.usage_epoch = next_epoch;
    s_maintenance_command_hash = hash;
    if (App_W25Qxx_Maintenance_Save() != W25Q_OK) {
        g_maintenance = previous;
        s_maintenance_command_hash = previous_hash;
        return W25Q_ERR;
    }
    *saved = g_maintenance;
    return W25Q_OK;
}

void App_W25Qxx_Storage_Load(void)
{
    w25q_storage_t slot_a;
    w25q_storage_t slot_b;
    uint8_t a_ok = storage_read_slot(&slot_a, W25Q_SLOT_A_ADDR);
    uint8_t b_ok = storage_read_slot(&slot_b, W25Q_SLOT_B_ADDR);

    if ((a_ok == W25Q_OK) && (b_ok == W25Q_OK))
    {
        g_w25q_storage = (slot_b.debug_counter > slot_a.debug_counter) ? slot_b : slot_a;
    }
    else if (a_ok == W25Q_OK)
    {
        g_w25q_storage = slot_a;
    }
    else if (b_ok == W25Q_OK)
    {
        g_w25q_storage = slot_b;
    }
    else
    {
        g_w25q_storage.magic = W25Q_STORAGE_MAGIC;
        g_w25q_storage.debug_counter = 0U;
        g_w25q_storage.crc = storage_crc(&g_w25q_storage);
    }
}

uint8_t App_W25Qxx_Storage_Save(void)
{
    w25q_storage_t slot_a;
    w25q_storage_t slot_b;
    uint8_t a_ok = storage_read_slot(&slot_a, W25Q_SLOT_A_ADDR);
    uint8_t b_ok = storage_read_slot(&slot_b, W25Q_SLOT_B_ADDR);
    uint32_t target_addr;

    g_w25q_storage.magic = W25Q_STORAGE_MAGIC;
    g_w25q_storage.crc = storage_crc(&g_w25q_storage);

    if ((a_ok != W25Q_OK) && (b_ok != W25Q_OK))
    {
        target_addr = W25Q_SLOT_A_ADDR;
    }
    else if (a_ok != W25Q_OK)
    {
        target_addr = W25Q_SLOT_A_ADDR;
    }
    else if (b_ok != W25Q_OK)
    {
        target_addr = W25Q_SLOT_B_ADDR;
    }
    else if (slot_a.debug_counter <= slot_b.debug_counter)
    {
        target_addr = W25Q_SLOT_A_ADDR;
    }
    else
    {
        target_addr = W25Q_SLOT_B_ADDR;
    }

    if (W25Q_Sector_Erase(&W25Q_Flash, target_addr) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    return W25Q_Page_Program(&W25Q_Flash, target_addr,
                             (const uint8_t *)&g_w25q_storage,
                             (uint16_t)W25Q_STORAGE_SIZE);
}

void App_W25Qxx_Height_Load(void)
{
}

uint8_t App_W25Qxx_Height_Save(void)
{
    return W25Q_OK;
}

void App_W25Qxx_Height_Save_If_Needed(void)
{
}

uint8_t App_W25Qxx_Height_Is_Loaded(void)
{
    return 0U;
}

void App_W25Qxx_Config_Load(void)
{
    w25q_config_t slot_a;
    w25q_config_t slot_b;
    uint8_t a_ok = config_read_slot(W25Q_CONFIG_SLOT_A_ADDR, &slot_a);
    uint8_t b_ok = config_read_slot(W25Q_CONFIG_SLOT_B_ADDR, &slot_b);

    if ((a_ok == W25Q_OK) && (b_ok == W25Q_OK))
    {
        g_config = slot_b;
        s_config_active_slot = 1U;
    }
    else if (a_ok == W25Q_OK)
    {
        g_config = slot_a;
        s_config_active_slot = 0U;
    }
    else if (b_ok == W25Q_OK)
    {
        g_config = slot_b;
        s_config_active_slot = 1U;
    }
}

uint8_t App_W25Qxx_Config_Save(void)
{
    uint32_t target_addr;
    w25q_config_t tmp;

    s_config_active_slot = (s_config_active_slot == 0U) ? 1U : 0U;
    target_addr = (s_config_active_slot == 0U) ?
                  W25Q_CONFIG_SLOT_A_ADDR : W25Q_CONFIG_SLOT_B_ADDR;

    g_config.header = W25Q_CONFIG_HEADER;
    g_config.version = W25Q_CONFIG_VERSION;
    tmp = g_config;
    tmp.crc16 = 0U;
    g_config.crc16 = App_W25Qxx_Crc16((const uint8_t *)&tmp, (uint32_t)sizeof(tmp));

    if (W25Q_Sector_Erase(&W25Q_Flash, target_addr) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    return W25Q_Page_Program(&W25Q_Flash, target_addr,
                             (const uint8_t *)&g_config,
                             (uint16_t)sizeof(g_config));
}

void App_W25Qxx_Stats_Load(void)
{
    w25q_stats_t slot_a;
    w25q_stats_t slot_b;
    uint8_t a_ok = stats_read_slot(W25Q_STATS_SLOT_A_ADDR, &slot_a);
    uint8_t b_ok = stats_read_slot(W25Q_STATS_SLOT_B_ADDR, &slot_b);

    if ((a_ok == W25Q_OK) && (b_ok == W25Q_OK))
    {
        if (stats_activity(&slot_b) >= stats_activity(&slot_a))
        {
            g_stats = slot_b;
            s_stats_active_slot = 1U;
        }
        else
        {
            g_stats = slot_a;
            s_stats_active_slot = 0U;
        }
    }
    else if (a_ok == W25Q_OK)
    {
        g_stats = slot_a;
        s_stats_active_slot = 0U;
    }
    else if (b_ok == W25Q_OK)
    {
        g_stats = slot_b;
        s_stats_active_slot = 1U;
    }
    else
    {
        memset(&g_stats, 0, sizeof(g_stats));
        g_stats.magic = W25Q_STATS_MAGIC;
        g_stats.version = W25Q_STATS_VERSION;
    }
}

uint8_t App_W25Qxx_Stats_Save(void)
{
    uint32_t target_addr;

    /* RiseStats 仍是 up_count 主路径；此处落盘完整 g_stats 双槽 */
    s_stats_active_slot = (s_stats_active_slot == 0U) ? 1U : 0U;
    target_addr = (s_stats_active_slot == 0U) ?
                  W25Q_STATS_SLOT_A_ADDR : W25Q_STATS_SLOT_B_ADDR;

    g_stats.magic = W25Q_STATS_MAGIC;
    g_stats.version = W25Q_STATS_VERSION;
    g_stats.crc = App_W25Qxx_Crc32((const uint8_t *)&g_stats,
                                   (uint32_t)offsetof(w25q_stats_t, crc));

    if (W25Q_Sector_Erase(&W25Q_Flash, target_addr) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    return W25Q_Page_Program(&W25Q_Flash, target_addr,
                             (const uint8_t *)&g_stats,
                             (uint16_t)sizeof(g_stats));
}

void App_W25Qxx_Stats_Inc_Up(lift_role_t role)
{
    g_stats.up_count++;
    if (role == LIFT_ROLE_MAIN)
    {
        g_stats.up_count_main++;
    }
    else
    {
        g_stats.up_count_sub++;
    }
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_Down(lift_role_t role)
{
    g_stats.down_count++;
    if (role == LIFT_ROLE_MAIN)
    {
        g_stats.down_count_main++;
    }
    else
    {
        g_stats.down_count_sub++;
    }
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_Lock(void)
{
    g_stats.lock_count++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_Refill(void)
{
    g_stats.refill_count++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_Estop(void)
{
    g_stats.estop_count++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_PhotoAlarm(void)
{
    g_stats.photo_alarm_count++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Add_RunMs(uint32_t ms)
{
    g_stats.total_run_ms += ms;
    (void)App_W25Qxx_Stats_Save();
}

uint8_t App_W25Qxx_OpLog_Append(const w25q_op_log_entry_t *entry)
{
    uint32_t addr;

    if (entry == NULL)
    {
        return W25Q_ERR;
    }
    if (s_oplog_scanned == 0U)
    {
        oplog_scan();
    }

    if (s_oplog_count >= W25Q_OPLOG_MAX_ENTRIES)
    {
        if (W25Q_Sector_Erase(&W25Q_Flash, W25Q_OPLOG_ADDR) != W25Q_OK)
        {
            return W25Q_ERR;
        }
        s_oplog_write_index = 0U;
        s_oplog_count = 0U;
    }

    addr = W25Q_OPLOG_ADDR + ((uint32_t)s_oplog_write_index * W25Q_OPLOG_ENTRY_SIZE);
    if (W25Q_Page_Program(&W25Q_Flash, addr, (const uint8_t *)entry,
                          (uint16_t)sizeof(*entry)) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    s_oplog_write_index = (uint16_t)((s_oplog_write_index + 1U) % W25Q_OPLOG_MAX_ENTRIES);
    if (s_oplog_count < W25Q_OPLOG_MAX_ENTRIES)
    {
        s_oplog_count++;
    }
    return W25Q_OK;
}

uint8_t App_W25Qxx_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out)
{
    uint32_t addr;
    uint16_t start;

    if ((out == NULL) || (index >= W25Q_OPLOG_MAX_ENTRIES))
    {
        return W25Q_ERR;
    }
    if (s_oplog_scanned == 0U)
    {
        oplog_scan();
    }
    if (index >= s_oplog_count)
    {
        memset(out, 0, sizeof(*out));
        return W25Q_ERR;
    }

    if (s_oplog_count >= W25Q_OPLOG_MAX_ENTRIES)
    {
        start = s_oplog_write_index;
    }
    else
    {
        start = 0U;
    }
    addr = W25Q_OPLOG_ADDR +
           ((((uint32_t)start + index) % W25Q_OPLOG_MAX_ENTRIES) * W25Q_OPLOG_ENTRY_SIZE);
    return W25Q_Read_Buffer(&W25Q_Flash, addr, (uint8_t *)out, (uint16_t)sizeof(*out));
}

uint16_t App_W25Qxx_OpLog_Count(void)
{
    if (s_oplog_scanned == 0U)
    {
        oplog_scan();
    }
    return s_oplog_count;
}

void App_W25Qxx_OpLog_Clear(void)
{
    if (W25Q_Sector_Erase(&W25Q_Flash, W25Q_OPLOG_ADDR) == W25Q_OK)
    {
        s_oplog_write_index = 0U;
        s_oplog_count = 0U;
        s_oplog_scanned = 1U;
    }
}

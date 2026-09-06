#include "app_w25qxx.h"
#include "app_spi.h"
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
    .bus = &SPI_Bus
};

static uint8_t s_rise_stats_scanned = 0U;
static uint32_t s_rise_journal_next_addr = W25Q_RISE_STATS_JOURNAL_ADDR;
static uint32_t s_rise_journal_sequence = 0U;
static uint8_t s_maintenance_scanned = 0U;
static uint8_t s_maintenance_found = 0U;
static uint32_t s_maintenance_journal_next_addr = W25Q_MAINTENANCE_JOURNAL_ADDR;
static uint32_t s_maintenance_journal_sequence = 0U;
static uint32_t s_maintenance_command_hash = 0U;

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

    App_SPI_System_Init();
    ret = W25Q_Init_Device(&W25Q_Flash);

    if (ret != W25Q_OK)
    {
        #if W25Q_DEBUG == 1
        elog_e("W25Q", "[W25Q] chip init FAILED (SPI comm error)");
        #endif
        return;
    }

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
    elog_i("W25Q", "[W25Q] JEDEC=0x%06lX DEV=0x%04X",
           W25Q_Read_JEDEC_ID(&W25Q_Flash),
           W25Q_Read_Device_ID(&W25Q_Flash));
    if (load_ret == W25Q_OK)
    {
        elog_i("W25Q", "[W25Q] rise total=%lu pending=%lu remainder=%u",
               (unsigned long)g_rise_stats.total_up_count,
               (unsigned long)g_rise_stats.pending_count,
               (unsigned int)g_rise_stats.remainder_ms);
    }
    else
    {
        elog_e("W25Q", "[W25Q] rise stats load FAILED");
    }
    if (maintenance_ret == W25Q_OK)
    {
        elog_i("W25Q", "[W25Q] maintenance total=%lu cycle=%lu due=%u epoch=%lu",
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
    (void)App_W25Qxx_RiseStats_Load();
}

uint8_t App_W25Qxx_Storage_Save(void)
{
    return App_W25Qxx_RiseStats_Save();
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
}

uint8_t App_W25Qxx_Config_Save(void)
{
    return W25Q_OK;
}

void App_W25Qxx_Stats_Load(void)
{
    (void)App_W25Qxx_RiseStats_Load();
}

uint8_t App_W25Qxx_Stats_Save(void)
{
    g_rise_stats.total_up_count = g_stats.up_count;
    return App_W25Qxx_RiseStats_Save();
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
}

void App_W25Qxx_Stats_Inc_Lock(void)
{
    g_stats.lock_count++;
}

void App_W25Qxx_Stats_Inc_Refill(void)
{
    g_stats.refill_count++;
}

void App_W25Qxx_Stats_Inc_Estop(void)
{
    g_stats.estop_count++;
}

void App_W25Qxx_Stats_Inc_PhotoAlarm(void)
{
    g_stats.photo_alarm_count++;
}

void App_W25Qxx_Stats_Add_RunMs(uint32_t ms)
{
    g_stats.total_run_ms += ms;
}

uint8_t App_W25Qxx_OpLog_Append(const w25q_op_log_entry_t *entry)
{
    (void)entry;
    return W25Q_OK;
}

uint8_t App_W25Qxx_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out)
{
    (void)index;
    if (out != NULL)
    {
        memset(out, 0, sizeof(*out));
    }
    return W25Q_ERR;
}

uint16_t App_W25Qxx_OpLog_Count(void)
{
    return 0U;
}

void App_W25Qxx_OpLog_Clear(void)
{
}

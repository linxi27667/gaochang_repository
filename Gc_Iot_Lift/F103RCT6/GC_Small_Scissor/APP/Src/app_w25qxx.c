/**
 * @file app_w25qxx.c
 * @brief W25Q APP wrapper for chip handshake plus in-memory config/stat/log stubs.
 */
#include "app_w25qxx.h"
#include "app_product.h"
#include <stddef.h>
#include <string.h>

#ifndef MAX_PULSES
#define MAX_PULSES 0U
#endif

#if W25Q_DEBUG == 1
#include "elog.h"
#endif

/* ============ 全局对象 ============
 * W25Q 当前仅用作芯片握手（验证 SPI 通信 + 读 JEDEC ID），
 * 不进行任何配置/统计/日志的持久化。
 * - g_product_type 直接用编译时默认值（GC-LiangZhu=DOUBLE_POST, GC-DaJian=LARGE_SCISSOR）
 * - g_config 保留内存默认值，供代码运行时引用
 * - 所有 Load/Save 接口保留空实现以兼容头文件声明，避免链接错误
 * - 配置/统计数据后续走云端 DTU 持久化
 */

w25q_storage_t g_w25q_storage = {0};

w25q_config_t g_config = {
    .header                   = W25Q_CONFIG_HEADER,
    .version                  = W25Q_CONFIG_VERSION,
    .product_type             = PRODUCT_TYPE_SMALL_SCISSOR,
    .reserved1                = 0,
    .motor_to_valve_delay_ms  = 200,
    .motor_hold_ms            = 3000,
    .sub_motor_hold_ms        = 1500,
    .module_enable_mask       = 0,
    .reserved2                = 0,
    .photoelectric_debounce_ms= 50,
    .estop_debounce_ms        = 20,
    /* 旧双柱字段（保留兼容） */
    .tolerance_up             = 4,
    .tolerance_down           = 4,
    .stall_timeout_ms         = 2000,
    .balance_wait_max_ms      = 10000,
    .collision_debounce_ms    = 50,
    .secondary_descent_pulses = 30,
    .dual_column_mode         = 1,
    .screw_lead_mm            = 8,
    .max_pulses               = MAX_PULSES,
};

w25q_stats_t g_stats = {0};

w25q_t W25Q_Flash = {
    .unused = 0
};

#define W25Q_RISE_COUNTER_MAGIC        0x52495345U /* "RISE" */
#define W25Q_RISE_COUNTER_VERSION      1U
#define W25Q_RISE_COUNTER_COMMITTED    0x00000000U
#define W25Q_RISE_COUNTER_ERASED_BYTE  0xFFU
#define W25Q_RISE_COUNTER_ERASED_WORD  0xFFFFFFFFU
/* Stats 结构占用槽首；RiseCounter journal 从偏移处追加，避免与 Stats 双槽互相覆盖 */
#define W25Q_RISE_COUNTER_SLOT_PREFIX  64U
#define W25Q_RISE_COUNTER_SLOT_SIZE    (0x0100U - W25Q_RISE_COUNTER_SLOT_PREFIX)
#define W25Q_EXPECTED_JEDEC_ID          0x00464D24UL

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t total_count;
    uint32_t remainder_ms;
    uint32_t crc;
    uint32_t commit;
} w25q_rise_counter_record_t;

#define W25Q_RISE_COUNTER_RECORDS_PER_SLOT \
    ((uint32_t)(W25Q_RISE_COUNTER_SLOT_SIZE / sizeof(w25q_rise_counter_record_t)))

typedef struct
{
    uint8_t latest_valid;
    uint8_t latest_slot;
    uint32_t next_index[2];
    w25q_rise_counter_record_t latest;
} w25q_rise_counter_scan_t;

static uint8_t s_w25q_flash_ready = 0U;
static volatile uint8_t s_rise_counter_save_active = 0U;
static uint8_t s_config_active_slot = 0U; /* 0=A, 1=B */
static uint8_t s_stats_active_slot = 0U;
static uint16_t s_oplog_write_index = 0U;
static uint16_t s_oplog_count = 0U;
static uint8_t s_oplog_scanned = 0U;

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

static uint32_t App_W25Qxx_Crc32_Bytes(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t bit;

    while (length-- != 0U)
    {
        crc ^= *data++;
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return ~crc;
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
    expect = App_W25Qxx_Crc32_Bytes((const uint8_t *)stats,
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

static uint8_t RiseCounter_Save_TryLock(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if (s_rise_counter_save_active != 0U)
    {
        __set_PRIMASK(primask);
        return 0U;
    }

    s_rise_counter_save_active = 1U;
    __set_PRIMASK(primask);
    return 1U;
}

static void RiseCounter_Save_Unlock(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    s_rise_counter_save_active = 0U;
    __set_PRIMASK(primask);
}

static uint32_t RiseCounter_Crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t bit;

    while (length-- != 0U)
    {
        crc ^= *data++;

        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

static uint8_t RiseCounter_Record_Is_Erased(const w25q_rise_counter_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t index;

    for (index = 0U; index < sizeof(*record); index++)
    {
        if (bytes[index] != W25Q_RISE_COUNTER_ERASED_BYTE)
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t RiseCounter_Record_Is_Valid(const w25q_rise_counter_record_t *record)
{
    if ((record->magic != W25Q_RISE_COUNTER_MAGIC) ||
        (record->version != W25Q_RISE_COUNTER_VERSION) ||
        (record->commit != W25Q_RISE_COUNTER_COMMITTED))
    {
        return 0U;
    }

    return (record->crc == RiseCounter_Crc32((const uint8_t *)record,
                                              (uint32_t)offsetof(w25q_rise_counter_record_t, crc))) ? 1U : 0U;
}

static uint8_t RiseCounter_Sequence_Is_Newer(uint32_t candidate, uint32_t current)
{
    return ((candidate != current) && ((candidate - current) < 0x80000000U)) ? 1U : 0U;
}

static uint32_t RiseCounter_Slot_Address(uint8_t slot)
{
    uint32_t base = (slot == 0U) ? W25Q_STATS_SLOT_A_ADDR : W25Q_STATS_SLOT_B_ADDR;
    return base + W25Q_RISE_COUNTER_SLOT_PREFIX;
}

static uint32_t RiseCounter_Slot_Erase_Address(uint8_t slot)
{
    return (slot == 0U) ? W25Q_STATS_SLOT_A_ADDR : W25Q_STATS_SLOT_B_ADDR;
}

static uint8_t RiseCounter_Erase_Slot(uint8_t slot)
{
    uint32_t base = RiseCounter_Slot_Erase_Address(slot);

    if (W25Q_Sector_Erase(&W25Q_Flash, base) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    /* 整槽擦除后写回 Stats 头部，journal 区已是 0xFF */
    g_stats.magic = W25Q_STATS_MAGIC;
    g_stats.version = W25Q_STATS_VERSION;
    g_stats.crc = App_W25Qxx_Crc32_Bytes((const uint8_t *)&g_stats,
                                         (uint32_t)offsetof(w25q_stats_t, crc));
    return W25Q_Page_Program(&W25Q_Flash, base,
                             (const uint8_t *)&g_stats,
                             (uint16_t)sizeof(g_stats));
}

static uint8_t RiseCounter_Scan(w25q_rise_counter_scan_t *scan)
{
    uint8_t slot;
    uint32_t index;

    memset(scan, 0, sizeof(*scan));

    for (slot = 0U; slot < 2U; slot++)
    {
        for (index = 0U; index < W25Q_RISE_COUNTER_RECORDS_PER_SLOT; index++)
        {
            w25q_rise_counter_record_t record;
            uint32_t address = RiseCounter_Slot_Address(slot) +
                               (index * sizeof(w25q_rise_counter_record_t));

            if (W25Q_Read_Buffer(&W25Q_Flash, address, (uint8_t *)&record,
                                 (uint16_t)sizeof(record)) != W25Q_OK)
            {
                return W25Q_ERR;
            }

            if (RiseCounter_Record_Is_Erased(&record) == 0U)
            {
                scan->next_index[slot] = index + 1U;
            }

            if ((RiseCounter_Record_Is_Valid(&record) != 0U) &&
                ((scan->latest_valid == 0U) ||
                 (RiseCounter_Sequence_Is_Newer(record.sequence,
                                                 scan->latest.sequence) != 0U)))
            {
                scan->latest_valid = 1U;
                scan->latest_slot = slot;
                scan->latest = record;
            }
        }
    }

    return W25Q_OK;
}

static uint8_t RiseCounter_Write_Record(uint8_t slot, uint32_t index,
                                        const w25q_rise_counter_record_t *record)
{
    uint32_t address = RiseCounter_Slot_Address(slot) +
                       (index * sizeof(w25q_rise_counter_record_t));
    uint32_t commit = W25Q_RISE_COUNTER_COMMITTED;
    w25q_rise_counter_record_t verify;

    if (W25Q_Write_MultiPage(&W25Q_Flash, address, (const uint8_t *)record,
                             (uint16_t)offsetof(w25q_rise_counter_record_t, commit)) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    /* The commit word is programmed separately after the CRC-protected fields. */
    if (W25Q_Write_MultiPage(&W25Q_Flash,
                             address + offsetof(w25q_rise_counter_record_t, commit),
                             (const uint8_t *)&commit, sizeof(commit)) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    if (W25Q_Read_Buffer(&W25Q_Flash, address, (uint8_t *)&verify,
                         (uint16_t)sizeof(verify)) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    return ((RiseCounter_Record_Is_Valid(&verify) != 0U) &&
            (verify.sequence == record->sequence) &&
            (verify.total_count == record->total_count) &&
            (verify.remainder_ms == record->remainder_ms)) ? W25Q_OK : W25Q_ERR;
}

/* ============ 系统初始化 ============ */
void App_W25Qxx_System_Init(void)
{
    uint8_t ret;
    uint32_t jedec_id;
    w25q_maintenance_ledger_t maintenance;

    s_w25q_flash_ready = 0U;
    ret = W25Q_Init_Device(&W25Q_Flash);
    jedec_id = W25Q_Read_JEDEC_ID(&W25Q_Flash);

    if (ret == W25Q_OK)
    {
        s_w25q_flash_ready = 1U;
        App_W25Qxx_Storage_Load();
        App_W25Qxx_Config_Load();
        App_W25Qxx_Stats_Load();
        (void)App_W25Qxx_Maintenance_Load(&maintenance);
        oplog_scan();
        #if W25Q_DEBUG == 1
        elog_i("W25Q", "[FRAM] JEDEC=0x%06lX DEV=0x%04X",
               jedec_id,
               W25Q_Read_Device_ID(&W25Q_Flash));
        elog_i("W25Q", "[FRAM] product_type=%d", g_product_type);
        #else
        (void)jedec_id;
        #endif
    }
    else
    {
        #if W25Q_DEBUG == 1
        elog_e("W25Q", "[FRAM] chip init failed ret=%u jedec=0x%06lX",
               (unsigned int)ret,
               (unsigned long)jedec_id);
        #endif
    }
}

uint8_t App_W25Qxx_RiseCounter_Load(uint32_t *total_count,
                                    uint32_t *remainder_ms,
                                    uint32_t *sequence)
{
    w25q_rise_counter_scan_t scan;

    if (total_count != NULL) *total_count = 0U;
    if (remainder_ms != NULL) *remainder_ms = 0U;
    if (sequence != NULL) *sequence = 0U;

    if (s_w25q_flash_ready == 0U)
    {
        return W25Q_ERR;
    }

    if (RiseCounter_Scan(&scan) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    if (scan.latest_valid != 0U)
    {
        if (total_count != NULL) *total_count = scan.latest.total_count;
        if (remainder_ms != NULL) *remainder_ms = scan.latest.remainder_ms;
        if (sequence != NULL) *sequence = scan.latest.sequence;
    }

    return W25Q_OK;
}

uint8_t App_W25Qxx_RiseCounter_Save(uint32_t total_count,
                                     uint32_t remainder_ms,
                                     uint32_t *sequence)
{
    w25q_rise_counter_scan_t scan;
    w25q_rise_counter_record_t record;
    uint8_t target_slot;
    uint32_t target_index;
    uint8_t result = W25Q_ERR;

    if (s_w25q_flash_ready == 0U)
    {
        return W25Q_ERR;
    }

    if (RiseCounter_Save_TryLock() == 0U)
    {
        return W25Q_ERR;
    }

    if (RiseCounter_Scan(&scan) != W25Q_OK)
    {
        goto done;
    }

    if (scan.latest_valid != 0U)
    {
        target_slot = scan.latest_slot;

        if (scan.next_index[target_slot] >= W25Q_RISE_COUNTER_RECORDS_PER_SLOT)
        {
            target_slot = (target_slot == 0U) ? 1U : 0U;

            if (scan.next_index[target_slot] >= W25Q_RISE_COUNTER_RECORDS_PER_SLOT)
            {
                /* Preserve the latest slot while recycling the older slot. */
                if (RiseCounter_Erase_Slot(target_slot) != W25Q_OK)
                {
                    goto done;
                }
                target_index = 0U;
            }
            else
            {
                target_index = scan.next_index[target_slot];
            }
        }
        else
        {
            target_index = scan.next_index[target_slot];
        }

        record.sequence = scan.latest.sequence + 1U;
    }
    else
    {
        target_slot = 0U;

        if (scan.next_index[target_slot] >= W25Q_RISE_COUNTER_RECORDS_PER_SLOT)
        {
            target_slot = 1U;
        }

        if (scan.next_index[target_slot] >= W25Q_RISE_COUNTER_RECORDS_PER_SLOT)
        {
            if (RiseCounter_Erase_Slot(target_slot) != W25Q_OK)
            {
                goto done;
            }
            target_index = 0U;
        }
        else
        {
            target_index = scan.next_index[target_slot];
        }

        record.sequence = 1U;
    }

    record.magic = W25Q_RISE_COUNTER_MAGIC;
    record.version = W25Q_RISE_COUNTER_VERSION;
    record.total_count = total_count;
    record.remainder_ms = remainder_ms;
    record.crc = RiseCounter_Crc32((const uint8_t *)&record,
                                   (uint32_t)offsetof(w25q_rise_counter_record_t, crc));
    record.commit = W25Q_RISE_COUNTER_ERASED_WORD;

    if (RiseCounter_Write_Record(target_slot, target_index, &record) != W25Q_OK)
    {
        goto done;
    }

    if (sequence != NULL)
    {
        *sequence = record.sequence;
    }
    result = W25Q_OK;

done:
    RiseCounter_Save_Unlock();
    return result;
}

/* The maintenance journal is intentionally separate from the old stats slots. */
#define W25Q_MAINTENANCE_MAGIC       0x4D41494EU /* "MAIN" */
#define W25Q_MAINTENANCE_VERSION     1U
#define W25Q_MAINTENANCE_COMMITTED   0x00000000U

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t total_lift_count;
    uint32_t maintenance_lift_count;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint32_t maintenance_due;
    uint32_t usage_epoch;
    uint32_t command_hashes[W25Q_MAINTENANCE_COMMAND_CACHE_SIZE];
    uint32_t crc;
    uint32_t commit;
} w25q_maintenance_record_t;

#define W25Q_MAINTENANCE_RECORDS_PER_SECTOR \
    ((uint32_t)(W25Q_MAINTENANCE_JOURNAL_SECTOR_SIZE / sizeof(w25q_maintenance_record_t)))

typedef struct
{
    uint8_t latest_valid;
    uint8_t latest_sector;
    uint32_t next_index[W25Q_MAINTENANCE_JOURNAL_SECTOR_COUNT];
    w25q_maintenance_record_t latest;
} w25q_maintenance_scan_t;

static uint32_t Maintenance_Sector_Address(uint8_t sector)
{
    return W25Q_MAINTENANCE_JOURNAL_ADDR +
           ((uint32_t)sector * W25Q_MAINTENANCE_JOURNAL_SECTOR_SIZE);
}

static uint8_t Maintenance_Record_Is_Erased(const w25q_maintenance_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t index;

    for (index = 0U; index < sizeof(*record); index++)
    {
        if (bytes[index] != W25Q_RISE_COUNTER_ERASED_BYTE)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t Maintenance_Record_Is_Valid(const w25q_maintenance_record_t *record)
{
    if ((record->magic != W25Q_MAINTENANCE_MAGIC) ||
        (record->version != W25Q_MAINTENANCE_VERSION) ||
        (record->commit != W25Q_MAINTENANCE_COMMITTED))
    {
        return 0U;
    }

    return (record->crc == RiseCounter_Crc32((const uint8_t *)record,
                                              (uint32_t)offsetof(w25q_maintenance_record_t, crc))) ? 1U : 0U;
}

static void Maintenance_Ledger_From_Record(w25q_maintenance_ledger_t *ledger,
                                           const w25q_maintenance_record_t *record)
{
    ledger->total_lift_count = record->total_lift_count;
    ledger->maintenance_lift_count = record->maintenance_lift_count;
    ledger->maintenance_count = record->maintenance_count;
    ledger->last_maintenance_total = record->last_maintenance_total;
    ledger->maintenance_due = record->maintenance_due;
    ledger->usage_epoch = record->usage_epoch;
    ledger->sequence = record->sequence;
    memcpy(ledger->command_hashes, record->command_hashes, sizeof(ledger->command_hashes));
}

static void Maintenance_Ledger_Zero(w25q_maintenance_ledger_t *ledger)
{
    memset(ledger, 0, sizeof(*ledger));
}

static uint8_t Maintenance_Scan(w25q_maintenance_scan_t *scan)
{
    uint8_t sector;
    uint32_t index;

    memset(scan, 0, sizeof(*scan));
    for (sector = 0U; sector < W25Q_MAINTENANCE_JOURNAL_SECTOR_COUNT; sector++)
    {
        for (index = 0U; index < W25Q_MAINTENANCE_RECORDS_PER_SECTOR; index++)
        {
            w25q_maintenance_record_t record;
            uint32_t address = Maintenance_Sector_Address(sector) +
                               (index * sizeof(w25q_maintenance_record_t));

            if (W25Q_Read_Buffer(&W25Q_Flash, address, (uint8_t *)&record,
                                 (uint16_t)sizeof(record)) != W25Q_OK)
            {
                return W25Q_ERR;
            }

            if (Maintenance_Record_Is_Erased(&record) == 0U)
            {
                scan->next_index[sector] = index + 1U;
            }

            if ((Maintenance_Record_Is_Valid(&record) != 0U) &&
                ((scan->latest_valid == 0U) ||
                 (RiseCounter_Sequence_Is_Newer(record.sequence,
                                                 scan->latest.sequence) != 0U)))
            {
                scan->latest_valid = 1U;
                scan->latest_sector = sector;
                scan->latest = record;
            }
        }
    }
    return W25Q_OK;
}

static uint8_t Maintenance_Append(const w25q_maintenance_ledger_t *ledger,
                                  w25q_maintenance_ledger_t *saved)
{
    w25q_maintenance_scan_t scan;
    w25q_maintenance_record_t record;
    w25q_maintenance_record_t verify;
    uint8_t sector;
    uint32_t index;
    uint32_t commit = W25Q_MAINTENANCE_COMMITTED;
    uint32_t address;

    if (Maintenance_Scan(&scan) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    sector = (scan.latest_valid != 0U) ? scan.latest_sector : 0U;
    if (scan.next_index[sector] >= W25Q_MAINTENANCE_RECORDS_PER_SECTOR)
    {
        sector = (uint8_t)((sector + 1U) % W25Q_MAINTENANCE_JOURNAL_SECTOR_COUNT);
        if (scan.next_index[sector] >= W25Q_MAINTENANCE_RECORDS_PER_SECTOR)
        {
            /* Erase only the oldest reusable sector; the latest record remains intact. */
            if (W25Q_Sector_Erase(&W25Q_Flash, Maintenance_Sector_Address(sector)) != W25Q_OK)
            {
                return W25Q_ERR;
            }
            index = 0U;
        }
        else
        {
            index = scan.next_index[sector];
        }
    }
    else
    {
        index = scan.next_index[sector];
    }

    memset(&record, 0, sizeof(record));
    record.magic = W25Q_MAINTENANCE_MAGIC;
    record.version = W25Q_MAINTENANCE_VERSION;
    record.sequence = (scan.latest_valid != 0U) ? (scan.latest.sequence + 1U) : 1U;
    record.total_lift_count = ledger->total_lift_count;
    record.maintenance_lift_count = ledger->maintenance_lift_count;
    record.maintenance_count = ledger->maintenance_count;
    record.last_maintenance_total = ledger->last_maintenance_total;
    record.maintenance_due = ledger->maintenance_due;
    record.usage_epoch = ledger->usage_epoch;
    memcpy(record.command_hashes, ledger->command_hashes, sizeof(record.command_hashes));
    record.crc = RiseCounter_Crc32((const uint8_t *)&record,
                                   (uint32_t)offsetof(w25q_maintenance_record_t, crc));
    record.commit = W25Q_RISE_COUNTER_ERASED_WORD;
    address = Maintenance_Sector_Address(sector) + (index * sizeof(record));

    if (W25Q_Write_MultiPage(&W25Q_Flash, address, (const uint8_t *)&record,
                             (uint16_t)offsetof(w25q_maintenance_record_t, commit)) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    if (W25Q_Write_MultiPage(&W25Q_Flash,
                             address + offsetof(w25q_maintenance_record_t, commit),
                             (const uint8_t *)&commit, sizeof(commit)) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    if (W25Q_Read_Buffer(&W25Q_Flash, address, (uint8_t *)&verify,
                         (uint16_t)sizeof(verify)) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    if ((Maintenance_Record_Is_Valid(&verify) == 0U) ||
        (verify.sequence != record.sequence))
    {
        return W25Q_ERR;
    }

    Maintenance_Ledger_From_Record(saved, &verify);
    return W25Q_OK;
}

static uint32_t Maintenance_Command_Hash(uint8_t command, const char *msg_id)
{
    uint32_t hash;

    if ((msg_id == NULL) || (msg_id[0] == '\0'))
    {
        return 0U;
    }
    hash = RiseCounter_Crc32((const uint8_t *)msg_id, (uint32_t)strlen(msg_id));
    return hash ^ ((uint32_t)command << 24) ^ 0xA5A55A5AU;
}

static uint8_t Maintenance_Command_Seen(const w25q_maintenance_ledger_t *ledger,
                                        uint32_t hash)
{
    uint32_t index;

    for (index = 0U; index < W25Q_MAINTENANCE_COMMAND_CACHE_SIZE; index++)
    {
        if ((hash != 0U) && (ledger->command_hashes[index] == hash))
        {
            return 1U;
        }
    }
    return 0U;
}

static void Maintenance_Remember_Command(w25q_maintenance_ledger_t *ledger,
                                         uint32_t hash)
{
    uint32_t index;

    for (index = W25Q_MAINTENANCE_COMMAND_CACHE_SIZE - 1U; index > 0U; index--)
    {
        ledger->command_hashes[index] = ledger->command_hashes[index - 1U];
    }
    ledger->command_hashes[0] = hash;
}

uint8_t App_W25Qxx_Maintenance_Load(w25q_maintenance_ledger_t *ledger)
{
    w25q_maintenance_scan_t scan;

    if (ledger == NULL)
    {
        return W25Q_ERR;
    }
    Maintenance_Ledger_Zero(ledger);
    if (s_w25q_flash_ready == 0U)
    {
        return W25Q_ERR;
    }
    if (Maintenance_Scan(&scan) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    if (scan.latest_valid != 0U)
    {
        Maintenance_Ledger_From_Record(ledger, &scan.latest);
    }
    return W25Q_OK;
}

static uint8_t Maintenance_Begin(w25q_maintenance_ledger_t *ledger)
{
    if ((ledger == NULL) || (s_w25q_flash_ready == 0U) ||
        (RiseCounter_Save_TryLock() == 0U))
    {
        return W25Q_ERR;
    }
    if (App_W25Qxx_Maintenance_Load(ledger) != W25Q_OK)
    {
        RiseCounter_Save_Unlock();
        return W25Q_ERR;
    }
    return W25Q_OK;
}

uint8_t App_W25Qxx_Maintenance_Increment(w25q_maintenance_ledger_t *ledger)
{
    w25q_maintenance_ledger_t current;
    uint8_t result;

    if (Maintenance_Begin(&current) != W25Q_OK)
    {
        return W25Q_ERR;
    }
    current.total_lift_count++;
    current.maintenance_lift_count++;
    current.maintenance_due = (current.maintenance_lift_count >= W25Q_MAINTENANCE_THRESHOLD) ? 1U : 0U;
    result = Maintenance_Append(&current, ledger);
    RiseCounter_Save_Unlock();
    return result;
}

uint8_t App_W25Qxx_Maintenance_Done(const char *msg_id,
                                    w25q_maintenance_ledger_t *ledger)
{
    w25q_maintenance_ledger_t current;
    uint32_t hash = Maintenance_Command_Hash(1U, msg_id);
    uint8_t result;

    if ((hash == 0U) || (Maintenance_Begin(&current) != W25Q_OK))
    {
        return W25Q_ERR;
    }
    if (Maintenance_Command_Seen(&current, hash) != 0U)
    {
        *ledger = current;
        RiseCounter_Save_Unlock();
        return W25Q_OK;
    }
    current.last_maintenance_total = current.total_lift_count;
    current.maintenance_lift_count = 0U;
    current.maintenance_count++;
    current.maintenance_due = 0U;
    Maintenance_Remember_Command(&current, hash);
    result = Maintenance_Append(&current, ledger);
    RiseCounter_Save_Unlock();
    return result;
}

uint8_t App_W25Qxx_Maintenance_ResetUsage(const char *msg_id,
                                          w25q_maintenance_ledger_t *ledger)
{
    w25q_maintenance_ledger_t current;
    uint32_t hash = Maintenance_Command_Hash(2U, msg_id);
    uint32_t epoch;
    uint8_t result;

    if ((hash == 0U) || (Maintenance_Begin(&current) != W25Q_OK))
    {
        return W25Q_ERR;
    }
    if (Maintenance_Command_Seen(&current, hash) != 0U)
    {
        *ledger = current;
        RiseCounter_Save_Unlock();
        return W25Q_OK;
    }
    epoch = current.usage_epoch + 1U;
    Maintenance_Ledger_Zero(&current);
    current.usage_epoch = epoch;
    Maintenance_Remember_Command(&current, hash);
    result = Maintenance_Append(&current, ledger);
    RiseCounter_Save_Unlock();
    return result;
}

uint32_t App_W25Qxx_Get_JEDEC_ID(void)
{
    return W25Q_Read_JEDEC_ID(&W25Q_Flash);
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
        /* 无 sequence 字段：优先 B（后写槽） */
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
    /* else 保留编译期默认 g_config */
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

    s_stats_active_slot = (s_stats_active_slot == 0U) ? 1U : 0U;
    target_addr = (s_stats_active_slot == 0U) ?
                  W25Q_STATS_SLOT_A_ADDR : W25Q_STATS_SLOT_B_ADDR;

    g_stats.magic = W25Q_STATS_MAGIC;
    g_stats.version = W25Q_STATS_VERSION;
    g_stats.crc = App_W25Qxx_Crc32_Bytes((const uint8_t *)&g_stats,
                                         (uint32_t)offsetof(w25q_stats_t, crc));

    /* FRAM 直接覆盖槽首；不擦整槽，保留同槽 RiseCounter journal（offset>=64） */
    return W25Q_Page_Program(&W25Q_Flash, target_addr,
                             (const uint8_t *)&g_stats,
                             (uint16_t)sizeof(g_stats));
}

void App_W25Qxx_Stats_Inc_Up(lift_role_t role)
{
    g_stats.up_count++;
    if (role == LIFT_ROLE_MAIN) g_stats.up_count_main++;
    else                        g_stats.up_count_sub++;
    (void)App_W25Qxx_Stats_Save();
}

void App_W25Qxx_Stats_Inc_Down(lift_role_t role)
{
    g_stats.down_count++;
    if (role == LIFT_ROLE_MAIN) g_stats.down_count_main++;
    else                        g_stats.down_count_sub++;
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

    /* 环满：整扇区擦除后从 0 重写（简单策略） */
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

    /* index=0 为最旧；环满时 write_index 指向最旧 */
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

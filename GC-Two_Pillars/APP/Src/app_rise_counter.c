#include "app_rise_counter.h"

#include "app_w25qxx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "elog.h"

#include <stddef.h>
#include <string.h>

#ifndef APP_RISE_COUNTER_DEBUG
#define APP_RISE_COUNTER_DEBUG 1
#endif

#if APP_RISE_COUNTER_DEBUG == 1
#define RISE_LOG_I(...) elog_i("RISE", __VA_ARGS__)
#define RISE_LOG_W(...) elog_w("RISE", __VA_ARGS__)
#define RISE_LOG_E(...) elog_e("RISE", __VA_ARGS__)
#else
#define RISE_LOG_I(...)
#define RISE_LOG_W(...)
#define RISE_LOG_E(...)
#endif

#define APP_RISE_COUNTER_PERIOD_MS             3000U

#define APP_RISE_JOURNAL_BASE_ADDR             W25Q_RISE_JOURNAL_ADDR
#define APP_RISE_JOURNAL_SECTOR_SIZE           W25Q_RISE_JOURNAL_SECTOR_SIZE
#define APP_RISE_JOURNAL_SECTOR_COUNT          W25Q_RISE_JOURNAL_SECTOR_COUNT
#define APP_RISE_JOURNAL_RECORD_SIZE           32U
#define APP_RISE_JOURNAL_RECORDS_PER_SECTOR    \
    (APP_RISE_JOURNAL_SECTOR_SIZE / APP_RISE_JOURNAL_RECORD_SIZE)
#define APP_RISE_JOURNAL_TOTAL_RECORDS         \
    (APP_RISE_JOURNAL_SECTOR_COUNT * APP_RISE_JOURNAL_RECORDS_PER_SECTOR)

#define APP_RISE_JOURNAL_MAGIC                 0x52495345UL /* "RISE" */
#define APP_RISE_JOURNAL_VERSION               1U
#define APP_RISE_JOURNAL_CRC_BYTES             30U

#define APP_MAINTENANCE_LEDGER_BASE_ADDR       W25Q_MAINTENANCE_LEDGER_ADDR
#define APP_MAINTENANCE_LEDGER_SECTOR_SIZE     W25Q_MAINTENANCE_LEDGER_SECTOR_SIZE
#define APP_MAINTENANCE_LEDGER_SECTOR_COUNT    W25Q_MAINTENANCE_LEDGER_SECTOR_COUNT
#define APP_MAINTENANCE_LEDGER_RECORD_SIZE     64U
#define APP_MAINTENANCE_LEDGER_RECORDS_PER_SECTOR \
    (APP_MAINTENANCE_LEDGER_SECTOR_SIZE / APP_MAINTENANCE_LEDGER_RECORD_SIZE)
#define APP_MAINTENANCE_LEDGER_TOTAL_RECORDS   \
    (APP_MAINTENANCE_LEDGER_SECTOR_COUNT * APP_MAINTENANCE_LEDGER_RECORDS_PER_SECTOR)
#define APP_MAINTENANCE_LEDGER_MAGIC           0x4D41494EUL /* "MAIN" */
#define APP_MAINTENANCE_LEDGER_VERSION_V1      1U
#define APP_MAINTENANCE_LEDGER_VERSION         2U
#define APP_MAINTENANCE_LEDGER_COMMIT          0x434D4954UL /* "CMIT" */
#define APP_MAINTENANCE_LEDGER_V1_CRC_BYTES    32U
#define APP_MAINTENANCE_COMMAND_DONE           1U
#define APP_MAINTENANCE_COMMAND_RESET          2U

#define APP_RISE_STORE_TASK_STACK_WORDS        384U
#define APP_RISE_STORE_TASK_PRIORITY           (tskIDLE_PRIORITY + 1U)
#define APP_RISE_STORE_RETRY_MS                5000U
#define APP_MAINTENANCE_COMMAND_TIMEOUT_MS      15000U

/* Seven 32-bit fields followed by version and CRC make this exactly 32 bytes
 * on both ARMCC and GCC without compiler-specific packing pragmas. */
typedef struct
{
    uint32_t magic;
    uint32_t journal_seq;
    uint32_t rise_total_ms;
    uint32_t rise_count;
    uint32_t rise_remainder_ms;
    uint32_t pending_count;
    uint32_t upload_seq;
    uint16_t version;
    uint16_t crc16;
} app_rise_journal_record_t;

typedef char app_rise_journal_record_size_must_be_32[
    (sizeof(app_rise_journal_record_t) == APP_RISE_JOURNAL_RECORD_SIZE) ? 1 : -1];

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t total_lift_count;
    uint32_t maintenance_lift_count;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint32_t usage_epoch;
    uint8_t maintenance_due;
    uint8_t reserved0;
    uint16_t version;
    uint32_t last_command_hash;
    uint8_t last_command_type;
    uint8_t reserved_command[3];
    uint32_t crc32;
    uint32_t commit;
    uint8_t reserved1[16];
} app_maintenance_ledger_record_t;

typedef struct
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t total_lift_count;
    uint32_t maintenance_lift_count;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint32_t usage_epoch;
    uint8_t maintenance_due;
    uint8_t reserved0;
    uint16_t version;
    uint32_t crc32;
    uint32_t commit;
    uint8_t reserved1[24];
} app_maintenance_ledger_record_v1_t;

typedef char app_maintenance_ledger_record_size_must_be_64[
    (sizeof(app_maintenance_ledger_record_t) == APP_MAINTENANCE_LEDGER_RECORD_SIZE) ? 1 : -1];

static volatile uint32_t s_rise_total_ms;
static volatile uint32_t s_rise_count;
static volatile uint32_t s_rise_remainder_ms;
static volatile uint32_t s_pending_count;
static volatile uint32_t s_upload_seq;
static volatile uint32_t s_pending_upload_epoch;
static volatile uint32_t s_state_revision;
static volatile uint32_t s_persisted_revision;
static volatile uint8_t s_flush_needed;
static volatile uint8_t s_local_changes_before_load;
static app_rise_counter_snapshot_t s_persisted_snapshot;
static app_maintenance_snapshot_t s_maintenance_snapshot;
static volatile uint32_t s_maintenance_state_revision;
static volatile uint32_t s_maintenance_persisted_revision;
static volatile uint8_t s_maintenance_flush_needed;

/* These fields are only touched from the lift control task. */
static uint8_t s_poll_started;
static uint8_t s_was_rising;
static uint32_t s_last_poll_tick;

/* These fields are only touched before scheduling or by the journal worker. */
static uint8_t s_journal_ready;
static uint32_t s_journal_sequence;
static uint32_t s_next_record_slot;
static uint8_t s_maintenance_ledger_ready;
static uint32_t s_maintenance_ledger_sequence;
static uint32_t s_maintenance_next_record_slot;
static uint32_t s_maintenance_command_hashes[APP_MAINTENANCE_LEDGER_TOTAL_RECORDS];
static uint8_t s_maintenance_command_types[APP_MAINTENANCE_LEDGER_TOTAL_RECORDS];
static uint32_t s_maintenance_command_count;
static TaskHandle_t s_store_task_handle;
static TaskHandle_t s_maintenance_command_waiter;
static uint32_t s_maintenance_command_target_revision;

/* A sector at a time is read during recovery, keeping the boot scan short and
 * avoiding a large task-stack allocation. */
static uint8_t s_scan_buffer[APP_RISE_JOURNAL_SECTOR_SIZE];

static void App_RiseCounter_StoreTask(void *argument);
static uint8_t RiseJournal_Load(uint8_t preserve_local_changes);
static uint8_t MaintenanceLedger_Load(uint8_t preserve_local_changes);
static uint32_t MaintenanceLedger_RecordAddress(uint32_t slot);

/* The RVDS FreeRTOS port initializes its critical-nesting state only when the
 * scheduler starts. Recovery runs earlier than that, so do not enter a kernel
 * critical section until a scheduler exists. */
static uint8_t RiseCounter_EnterCritical(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        taskENTER_CRITICAL();
        return 1U;
    }

    return 0U;
}

static void RiseCounter_ExitCritical(uint8_t entered)
{
    if (entered != 0U)
    {
        taskEXIT_CRITICAL();
    }
}

static uint16_t RiseCounter_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= (uint16_t)((uint16_t)data[index] << 8);
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

static uint32_t MaintenanceLedger_Crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }

    return ~crc;
}

static uint8_t RiseJournal_RecordIsValid(const app_rise_journal_record_t *record)
{
    if (record == NULL)
    {
        return 0U;
    }

    if ((record->magic != APP_RISE_JOURNAL_MAGIC) ||
        (record->version != APP_RISE_JOURNAL_VERSION) ||
        (record->rise_remainder_ms >= APP_RISE_COUNTER_PERIOD_MS))
    {
        return 0U;
    }

    return (RiseCounter_Crc16((const uint8_t *)record,
                              APP_RISE_JOURNAL_CRC_BYTES) == record->crc16) ? 1U : 0U;
}

static uint8_t RiseJournal_SequenceIsNewer(uint32_t candidate, uint32_t reference)
{
    return (((int32_t)(candidate - reference)) > 0) ? 1U : 0U;
}

static uint32_t RiseJournal_RecordAddress(uint32_t slot)
{
    return APP_RISE_JOURNAL_BASE_ADDR + (slot * APP_RISE_JOURNAL_RECORD_SIZE);
}

static uint32_t RiseJournal_SectorAddress(uint32_t sector)
{
    return APP_RISE_JOURNAL_BASE_ADDR + (sector * APP_RISE_JOURNAL_SECTOR_SIZE);
}

static uint8_t RiseJournal_IsErased(const uint8_t *data, uint16_t length)
{
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        if (data[index] != 0xFFU)
        {
            return 0U;
        }
    }

    return 1U;
}

static void RiseCounter_CopySnapshotNoLock(app_rise_counter_snapshot_t *out)
{
    out->rise_total_ms = s_rise_total_ms;
    out->rise_count = s_rise_count;
    out->rise_remainder_ms = s_rise_remainder_ms;
    out->pending_count = s_pending_count;
    out->upload_seq = s_upload_seq;
}

static void MaintenanceLedger_CopySnapshotNoLock(app_maintenance_snapshot_t *out)
{
    *out = s_maintenance_snapshot;
    out->maintenance_revision = s_maintenance_ledger_sequence;
}

static void RiseCounter_SetPersistedSnapshotNoLock(const app_rise_counter_snapshot_t *snapshot)
{
    s_persisted_snapshot.rise_total_ms = snapshot->rise_total_ms;
    s_persisted_snapshot.rise_count = snapshot->rise_count;
    s_persisted_snapshot.rise_remainder_ms = snapshot->rise_remainder_ms;
    s_persisted_snapshot.pending_count = snapshot->pending_count;
    s_persisted_snapshot.upload_seq = snapshot->upload_seq;
}

static void RiseCounter_RequestFlushNoLock(void)
{
    s_flush_needed = 1U;
    s_state_revision++;

    if (s_journal_ready == 0U)
    {
        s_local_changes_before_load = 1U;
    }
}

static void MaintenanceLedger_RequestFlushNoLock(void)
{
    s_maintenance_flush_needed = 1U;
    s_maintenance_state_revision++;
}

static void RiseCounter_NotifyStoreTask(void)
{
    TaskHandle_t task_handle;
    uint8_t critical_entered;

    critical_entered = RiseCounter_EnterCritical();
    task_handle = s_store_task_handle;
    RiseCounter_ExitCritical(critical_entered);

    if ((task_handle != NULL) &&
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED))
    {
        (void)xTaskNotifyGive(task_handle);
    }
}

static uint8_t RiseCounter_IsFlushNeeded(void)
{
    uint8_t needed;
    uint8_t critical_entered;

    critical_entered = RiseCounter_EnterCritical();
    needed = (uint8_t)((s_flush_needed != 0U) ||
                       (s_maintenance_flush_needed != 0U));
    RiseCounter_ExitCritical(critical_entered);

    return needed;
}

/* Find an erased record slot. A non-erased first slot of a sector belongs to
 * the previous journal lap, so that sector is erased only by this worker. */
static uint8_t RiseJournal_PrepareWriteSlot(uint32_t *slot_out)
{
    uint8_t record_data[APP_RISE_JOURNAL_RECORD_SIZE];
    uint32_t slot;
    uint32_t attempt;
    uint32_t sector;
    uint32_t record_in_sector;

    if (slot_out == NULL)
    {
        return 0U;
    }

    slot = s_next_record_slot;

    for (attempt = 0U; attempt < APP_RISE_JOURNAL_TOTAL_RECORDS; attempt++)
    {
        if (W25Q_Read_Buffer(&W25Q_Flash,
                             RiseJournal_RecordAddress(slot),
                             record_data,
                             APP_RISE_JOURNAL_RECORD_SIZE) != W25Q_OK)
        {
            RISE_LOG_E("[RISE] journal slot read failed at 0x%08lX",
                       (unsigned long)RiseJournal_RecordAddress(slot));
            return 0U;
        }

        if (RiseJournal_IsErased(record_data, APP_RISE_JOURNAL_RECORD_SIZE) != 0U)
        {
            *slot_out = slot;
            return 1U;
        }

        record_in_sector = slot % APP_RISE_JOURNAL_RECORDS_PER_SECTOR;
        if (record_in_sector == 0U)
        {
            sector = slot / APP_RISE_JOURNAL_RECORDS_PER_SECTOR;
            if (W25Q_Sector_Erase(&W25Q_Flash, RiseJournal_SectorAddress(sector)) != W25Q_OK)
            {
                RISE_LOG_E("[RISE] journal erase failed at sector %lu",
                           (unsigned long)sector);
                return 0U;
            }

            *slot_out = slot;
            return 1U;
        }

        slot++;
        if (slot >= APP_RISE_JOURNAL_TOTAL_RECORDS)
        {
            slot = 0U;
        }
    }

    RISE_LOG_E("[RISE] no writable journal slot");
    return 0U;
}

static uint8_t RiseJournal_Append(const app_rise_counter_snapshot_t *snapshot)
{
    app_rise_journal_record_t record;
    app_rise_journal_record_t verify_record;
    uint32_t slot;
    uint32_t next_slot;

    if (snapshot == NULL)
    {
        return 0U;
    }

    if (RiseJournal_PrepareWriteSlot(&slot) == 0U)
    {
        return 0U;
    }

    memset(&record, 0, sizeof(record));
    record.magic = APP_RISE_JOURNAL_MAGIC;
    record.journal_seq = s_journal_sequence + 1U;
    record.rise_total_ms = snapshot->rise_total_ms;
    record.rise_count = snapshot->rise_count;
    record.rise_remainder_ms = snapshot->rise_remainder_ms;
    record.pending_count = snapshot->pending_count;
    record.upload_seq = snapshot->upload_seq;
    record.version = APP_RISE_JOURNAL_VERSION;
    record.crc16 = RiseCounter_Crc16((const uint8_t *)&record,
                                     APP_RISE_JOURNAL_CRC_BYTES);

    /* Advance the in-RAM sequence before programming. If a program operation
     * reports an error after committing, a retry will still have a newer seq. */
    s_journal_sequence = record.journal_seq;

    if (W25Q_Page_Program(&W25Q_Flash,
                          RiseJournal_RecordAddress(slot),
                          (const uint8_t *)&record,
                          APP_RISE_JOURNAL_RECORD_SIZE) != W25Q_OK)
    {
        RISE_LOG_E("[RISE] journal write failed at 0x%08lX",
                   (unsigned long)RiseJournal_RecordAddress(slot));
        return 0U;
    }

    if (W25Q_Read_Buffer(&W25Q_Flash,
                         RiseJournal_RecordAddress(slot),
                         (uint8_t *)&verify_record,
                         APP_RISE_JOURNAL_RECORD_SIZE) != W25Q_OK ||
        RiseJournal_RecordIsValid(&verify_record) == 0U ||
        verify_record.journal_seq != record.journal_seq)
    {
        RISE_LOG_E("[RISE] journal verify failed at 0x%08lX",
                   (unsigned long)RiseJournal_RecordAddress(slot));
        return 0U;
    }

    next_slot = slot + 1U;
    if (next_slot >= APP_RISE_JOURNAL_TOTAL_RECORDS)
    {
        next_slot = 0U;
    }
    s_next_record_slot = next_slot;

    return 1U;
}

static uint8_t MaintenanceLedger_RecordIsValid(const app_maintenance_ledger_record_t *record)
{
    uint32_t crc_bytes;
    const app_maintenance_ledger_record_v1_t *v1;
    if (record == NULL)
    {
        return 0U;
    }

    if ((record->magic != APP_MAINTENANCE_LEDGER_MAGIC) ||
        (record->maintenance_due > 1U))
    {
        return 0U;
    }

    if (record->version == APP_MAINTENANCE_LEDGER_VERSION_V1)
    {
        v1 = (const app_maintenance_ledger_record_v1_t *)record;
        return ((v1->commit == APP_MAINTENANCE_LEDGER_COMMIT) &&
                (MaintenanceLedger_Crc32((const uint8_t *)v1,
                                         APP_MAINTENANCE_LEDGER_V1_CRC_BYTES) == v1->crc32)) ? 1U : 0U;
    }
    if ((record->version != APP_MAINTENANCE_LEDGER_VERSION) ||
        (record->commit != APP_MAINTENANCE_LEDGER_COMMIT))
    {
        return 0U;
    }

    crc_bytes = (uint32_t)offsetof(app_maintenance_ledger_record_t, crc32);
    return (MaintenanceLedger_Crc32((const uint8_t *)record, crc_bytes) == record->crc32) ? 1U : 0U;
}

static uint32_t MaintenanceLedger_CommandHash(uint8_t command, const char *msg_id)
{
    uint32_t hash;

    if ((msg_id == NULL) || (msg_id[0] == '\0'))
    {
        return 0U;
    }
    hash = MaintenanceLedger_Crc32((const uint8_t *)msg_id,
                                   (uint16_t)strlen(msg_id));
    hash ^= ((uint32_t)command * 0x9E3779B9UL);
    return (hash != 0U) ? hash : 0xFFFFFFFFUL;
}

static uint8_t MaintenanceLedger_CommandSeen(uint8_t command, uint32_t command_hash)
{
    uint32_t index;

    for (index = 0U; index < s_maintenance_command_count; ++index)
    {
        if ((s_maintenance_command_types[index] == command) &&
            (s_maintenance_command_hashes[index] == command_hash))
        {
            return 1U;
        }
    }
    return 0U;
}

static void MaintenanceLedger_RememberCommand(uint8_t command, uint32_t command_hash)
{
    if (((command != APP_MAINTENANCE_COMMAND_DONE) &&
         (command != APP_MAINTENANCE_COMMAND_RESET)) ||
        (command_hash == 0U) ||
        (MaintenanceLedger_CommandSeen(command, command_hash) != 0U))
    {
        return;
    }
    if (s_maintenance_command_count < APP_MAINTENANCE_LEDGER_TOTAL_RECORDS)
    {
        s_maintenance_command_types[s_maintenance_command_count] = command;
        s_maintenance_command_hashes[s_maintenance_command_count] = command_hash;
        s_maintenance_command_count++;
    }
}

static uint32_t MaintenanceLedger_RecordAddress(uint32_t slot)
{
    return APP_MAINTENANCE_LEDGER_BASE_ADDR +
           (slot * APP_MAINTENANCE_LEDGER_RECORD_SIZE);
}

static uint32_t MaintenanceLedger_SectorAddress(uint32_t sector)
{
    return APP_MAINTENANCE_LEDGER_BASE_ADDR +
           (sector * APP_MAINTENANCE_LEDGER_SECTOR_SIZE);
}

static uint8_t MaintenanceLedger_PrepareWriteSlot(uint32_t *slot_out)
{
    uint8_t record_data[APP_MAINTENANCE_LEDGER_RECORD_SIZE];
    uint32_t slot;
    uint32_t attempt;
    uint32_t sector;

    if (slot_out == NULL)
    {
        return 0U;
    }

    slot = s_maintenance_next_record_slot;
    for (attempt = 0U; attempt < APP_MAINTENANCE_LEDGER_TOTAL_RECORDS; attempt++)
    {
        if (W25Q_Read_Buffer(&W25Q_Flash,
                             MaintenanceLedger_RecordAddress(slot),
                             record_data,
                             APP_MAINTENANCE_LEDGER_RECORD_SIZE) != W25Q_OK)
        {
            RISE_LOG_E("[RISE] maintenance slot read failed at 0x%08lX",
                       (unsigned long)MaintenanceLedger_RecordAddress(slot));
            return 0U;
        }

        if (RiseJournal_IsErased(record_data, APP_MAINTENANCE_LEDGER_RECORD_SIZE) != 0U)
        {
            *slot_out = slot;
            return 1U;
        }

        if ((slot % APP_MAINTENANCE_LEDGER_RECORDS_PER_SECTOR) == 0U)
        {
            sector = slot / APP_MAINTENANCE_LEDGER_RECORDS_PER_SECTOR;
            if (W25Q_Sector_Erase(&W25Q_Flash,
                                  MaintenanceLedger_SectorAddress(sector)) != W25Q_OK)
            {
                RISE_LOG_E("[RISE] maintenance erase failed at sector %lu",
                           (unsigned long)sector);
                return 0U;
            }

            *slot_out = slot;
            return 1U;
        }

        slot++;
        if (slot >= APP_MAINTENANCE_LEDGER_TOTAL_RECORDS)
        {
            slot = 0U;
        }
    }

    return 0U;
}

static uint8_t MaintenanceLedger_Append(const app_maintenance_snapshot_t *snapshot)
{
    app_maintenance_ledger_record_t record;
    app_maintenance_ledger_record_t verify_record;
    uint32_t slot;

    if ((snapshot == NULL) || (MaintenanceLedger_PrepareWriteSlot(&slot) == 0U))
    {
        return 0U;
    }

    memset(&record, 0, sizeof(record));
    record.magic = APP_MAINTENANCE_LEDGER_MAGIC;
    record.sequence = s_maintenance_ledger_sequence + 1U;
    record.total_lift_count = snapshot->total_lift_count;
    record.maintenance_lift_count = snapshot->maintenance_lift_count;
    record.maintenance_count = snapshot->maintenance_count;
    record.last_maintenance_total = snapshot->last_maintenance_total;
    record.usage_epoch = snapshot->usage_epoch;
    record.maintenance_due = snapshot->maintenance_due;
    record.version = APP_MAINTENANCE_LEDGER_VERSION;
    record.last_command_hash = snapshot->last_command_hash;
    record.last_command_type = snapshot->last_command_type;
    record.crc32 = MaintenanceLedger_Crc32((const uint8_t *)&record,
                                           (uint16_t)offsetof(app_maintenance_ledger_record_t, crc32));
    record.commit = APP_MAINTENANCE_LEDGER_COMMIT;

    s_maintenance_ledger_sequence = record.sequence;
    /* The commit word is programmed last. Power loss before this write leaves
     * a record that recovery deliberately ignores. */
    if ((W25Q_Page_Program(&W25Q_Flash,
                           MaintenanceLedger_RecordAddress(slot),
                           (const uint8_t *)&record,
                           (uint16_t)offsetof(app_maintenance_ledger_record_t, commit)) != W25Q_OK) ||
        (W25Q_Page_Program(&W25Q_Flash,
                           MaintenanceLedger_RecordAddress(slot) +
                               offsetof(app_maintenance_ledger_record_t, commit),
                           (const uint8_t *)&record.commit,
                           sizeof(record.commit)) != W25Q_OK) ||
        (W25Q_Read_Buffer(&W25Q_Flash,
                          MaintenanceLedger_RecordAddress(slot),
                          (uint8_t *)&verify_record,
                          APP_MAINTENANCE_LEDGER_RECORD_SIZE) != W25Q_OK) ||
        (MaintenanceLedger_RecordIsValid(&verify_record) == 0U) ||
        (verify_record.sequence != record.sequence))
    {
        RISE_LOG_E("[RISE] maintenance ledger write/verify failed at 0x%08lX",
                   (unsigned long)MaintenanceLedger_RecordAddress(slot));
        return 0U;
    }

    s_maintenance_next_record_slot = slot + 1U;
    if (s_maintenance_next_record_slot >= APP_MAINTENANCE_LEDGER_TOTAL_RECORDS)
    {
        s_maintenance_next_record_slot = 0U;
    }
    MaintenanceLedger_RememberCommand(record.last_command_type,
                                      record.last_command_hash);
    return 1U;
}

static uint8_t MaintenanceLedger_Load(uint8_t preserve_local_changes)
{
    app_maintenance_ledger_record_t candidate;
    app_maintenance_ledger_record_t latest;
    uint32_t sector;
    uint32_t index;
    uint32_t latest_slot = 0U;
    uint32_t local_total = 0U;
    uint32_t local_cycle = 0U;
    uint8_t have_latest = 0U;
    uint8_t merge_local = 0U;
    uint8_t critical_entered;

    memset(&latest, 0, sizeof(latest));
    s_maintenance_command_count = 0U;
    for (sector = 0U; sector < APP_MAINTENANCE_LEDGER_SECTOR_COUNT; sector++)
    {
        if (W25Q_Read_Buffer(&W25Q_Flash,
                             MaintenanceLedger_SectorAddress(sector),
                             s_scan_buffer,
                             APP_MAINTENANCE_LEDGER_SECTOR_SIZE) != W25Q_OK)
        {
            return 0U;
        }

        for (index = 0U; index < APP_MAINTENANCE_LEDGER_RECORDS_PER_SECTOR; index++)
        {
            memcpy(&candidate,
                   &s_scan_buffer[index * APP_MAINTENANCE_LEDGER_RECORD_SIZE],
                   sizeof(candidate));
            if ((MaintenanceLedger_RecordIsValid(&candidate) != 0U) &&
                ((have_latest == 0U) ||
                 (RiseJournal_SequenceIsNewer(candidate.sequence, latest.sequence) != 0U)))
            {
                latest = candidate;
                latest_slot = (sector * APP_MAINTENANCE_LEDGER_RECORDS_PER_SECTOR) + index;
                have_latest = 1U;
            }
            if ((MaintenanceLedger_RecordIsValid(&candidate) != 0U) &&
                (candidate.version == APP_MAINTENANCE_LEDGER_VERSION))
            {
                MaintenanceLedger_RememberCommand(candidate.last_command_type,
                                                  candidate.last_command_hash);
            }
        }
    }

    critical_entered = RiseCounter_EnterCritical();
    if ((preserve_local_changes != 0U) && (s_maintenance_flush_needed != 0U))
    {
        merge_local = 1U;
        local_total = s_maintenance_snapshot.total_lift_count;
        local_cycle = s_maintenance_snapshot.maintenance_lift_count;
    }
    if (have_latest != 0U)
    {
        s_maintenance_snapshot.total_lift_count = latest.total_lift_count;
        s_maintenance_snapshot.maintenance_lift_count = latest.maintenance_lift_count;
        s_maintenance_snapshot.maintenance_count = latest.maintenance_count;
        s_maintenance_snapshot.last_maintenance_total = latest.last_maintenance_total;
        s_maintenance_snapshot.usage_epoch = latest.usage_epoch;
        s_maintenance_snapshot.maintenance_due = latest.maintenance_due;
        if (latest.version == APP_MAINTENANCE_LEDGER_VERSION)
        {
            s_maintenance_snapshot.last_command_hash = latest.last_command_hash;
            s_maintenance_snapshot.last_command_type = latest.last_command_type;
        }
        else
        {
            s_maintenance_snapshot.last_command_hash = 0U;
            s_maintenance_snapshot.last_command_type = 0U;
        }
        s_maintenance_ledger_sequence = latest.sequence;
        s_maintenance_next_record_slot = latest_slot + 1U;
        if (s_maintenance_next_record_slot >= APP_MAINTENANCE_LEDGER_TOTAL_RECORDS)
        {
            s_maintenance_next_record_slot = 0U;
        }
    }
    else
    {
        memset(&s_maintenance_snapshot, 0, sizeof(s_maintenance_snapshot));
        s_maintenance_ledger_sequence = 0U;
        s_maintenance_next_record_slot = 0U;
    }
    if (merge_local != 0U)
    {
        s_maintenance_snapshot.total_lift_count += local_total;
        s_maintenance_snapshot.maintenance_lift_count += local_cycle;
        if (s_maintenance_snapshot.maintenance_lift_count >= APP_MAINTENANCE_THRESHOLD)
        {
            s_maintenance_snapshot.maintenance_due = 1U;
        }
        s_maintenance_flush_needed = 1U;
        s_maintenance_state_revision++;
    }
    else
    {
        s_maintenance_flush_needed = 0U;
        s_maintenance_state_revision = 0U;
    }
    s_maintenance_persisted_revision = 0U;
    s_maintenance_ledger_ready = 1U;
    RiseCounter_ExitCritical(critical_entered);

    RISE_LOG_I("[RISE] maintenance ledger %s total=%lu cycle=%lu epoch=%lu",
               (have_latest != 0U) ? "restored" : "empty",
               (unsigned long)s_maintenance_snapshot.total_lift_count,
               (unsigned long)s_maintenance_snapshot.maintenance_lift_count,
               (unsigned long)s_maintenance_snapshot.usage_epoch);
    return 1U;
}

/* The full scan runs before the lift task starts. A retry from the worker
 * preserves any local accumulation that occurred while W25Q was unavailable. */
static uint8_t RiseJournal_Load(uint8_t preserve_local_changes)
{
    app_rise_journal_record_t candidate;
    app_rise_journal_record_t latest;
    uint32_t sector;
    uint32_t index;
    uint32_t slot;
    uint32_t latest_slot = 0U;
    uint8_t have_latest = 0U;
    uint8_t merge_local = 0U;
    uint32_t local_total = 0U;
    uint32_t local_count = 0U;
    uint32_t local_remainder = 0U;
    uint32_t local_pending = 0U;
    uint32_t remainder_sum;
    uint32_t carry_count;
    uint8_t critical_entered;

    memset(&latest, 0, sizeof(latest));

    for (sector = 0U; sector < APP_RISE_JOURNAL_SECTOR_COUNT; sector++)
    {
        if (W25Q_Read_Buffer(&W25Q_Flash,
                             RiseJournal_SectorAddress(sector),
                             s_scan_buffer,
                             APP_RISE_JOURNAL_SECTOR_SIZE) != W25Q_OK)
        {
            RISE_LOG_E("[RISE] boot scan failed at sector %lu",
                       (unsigned long)sector);
            return 0U;
        }

        for (index = 0U; index < APP_RISE_JOURNAL_RECORDS_PER_SECTOR; index++)
        {
            memcpy(&candidate,
                   &s_scan_buffer[index * APP_RISE_JOURNAL_RECORD_SIZE],
                   sizeof(candidate));

            if (RiseJournal_RecordIsValid(&candidate) == 0U)
            {
                continue;
            }

            slot = (sector * APP_RISE_JOURNAL_RECORDS_PER_SECTOR) + index;
            if ((have_latest == 0U) ||
                (RiseJournal_SequenceIsNewer(candidate.journal_seq,
                                              latest.journal_seq) != 0U))
            {
                latest = candidate;
                latest_slot = slot;
                have_latest = 1U;
            }
        }
    }

    if (have_latest != 0U)
    {
        s_journal_sequence = latest.journal_seq;
        s_next_record_slot = latest_slot + 1U;
        if (s_next_record_slot >= APP_RISE_JOURNAL_TOTAL_RECORDS)
        {
            s_next_record_slot = 0U;
        }
    }
    else
    {
        s_journal_sequence = 0U;
        s_next_record_slot = 0U;
    }

    critical_entered = RiseCounter_EnterCritical();
    if ((preserve_local_changes != 0U) &&
        (s_local_changes_before_load != 0U))
    {
        merge_local = 1U;
        local_total = s_rise_total_ms;
        local_count = s_rise_count;
        local_remainder = s_rise_remainder_ms;
        local_pending = s_pending_count;
    }

    if (have_latest != 0U)
    {
        s_rise_total_ms = latest.rise_total_ms;
        s_rise_count = latest.rise_count;
        s_rise_remainder_ms = latest.rise_remainder_ms;
        s_pending_count = latest.pending_count;
        s_upload_seq = latest.upload_seq;

        s_persisted_snapshot.rise_total_ms = latest.rise_total_ms;
        s_persisted_snapshot.rise_count = latest.rise_count;
        s_persisted_snapshot.rise_remainder_ms = latest.rise_remainder_ms;
        s_persisted_snapshot.pending_count = latest.pending_count;
        s_persisted_snapshot.upload_seq = latest.upload_seq;
    }
    else
    {
        s_rise_total_ms = 0U;
        s_rise_count = 0U;
        s_rise_remainder_ms = 0U;
        s_pending_count = 0U;
        s_upload_seq = 0U;

        memset(&s_persisted_snapshot, 0, sizeof(s_persisted_snapshot));
    }

    if (merge_local != 0U)
    {
        remainder_sum = s_rise_remainder_ms + local_remainder;
        carry_count = remainder_sum / APP_RISE_COUNTER_PERIOD_MS;
        s_rise_total_ms += local_total;
        s_rise_count += local_count + carry_count;
        s_rise_remainder_ms = remainder_sum % APP_RISE_COUNTER_PERIOD_MS;
        s_pending_count += local_pending + carry_count;
        s_upload_seq += local_count + carry_count;
        RiseCounter_RequestFlushNoLock();
    }
    else
    {
        s_flush_needed = 0U;
        s_state_revision = 0U;
    }

    s_local_changes_before_load = 0U;
    s_persisted_revision = 0U;
    s_journal_ready = 1U;

    /* g_stats keeps the legacy telemetry fields coherent with the qualified
     * time-based count. The old double-post immediate increment is removed by
     * the caller integration. */
    g_stats.up_count = s_rise_count;
    g_stats.up_count_main = s_rise_count;
    RiseCounter_ExitCritical(critical_entered);

    if (have_latest != 0U)
    {
        RISE_LOG_I("[RISE] restored total=%lu count=%lu remainder=%lu pending=%lu seq=%lu",
                   (unsigned long)latest.rise_total_ms,
                   (unsigned long)latest.rise_count,
                   (unsigned long)latest.rise_remainder_ms,
                   (unsigned long)latest.pending_count,
                   (unsigned long)latest.upload_seq);
    }
    else
    {
        RISE_LOG_I("[RISE] journal is empty");
    }

    return 1U;
}

static uint8_t RiseCounter_FlushOnce(void)
{
    app_rise_counter_snapshot_t snapshot;
    uint32_t revision;
    uint8_t critical_entered;

    critical_entered = RiseCounter_EnterCritical();
    if (s_flush_needed == 0U)
    {
        RiseCounter_ExitCritical(critical_entered);
        return 1U;
    }

    RiseCounter_CopySnapshotNoLock(&snapshot);
    revision = s_state_revision;
    RiseCounter_ExitCritical(critical_entered);

    if (RiseJournal_Append(&snapshot) == 0U)
    {
        return 0U;
    }

    critical_entered = RiseCounter_EnterCritical();
    RiseCounter_SetPersistedSnapshotNoLock(&snapshot);
    s_persisted_revision = revision;
    if (s_state_revision == revision)
    {
        s_flush_needed = 0U;
    }
    RiseCounter_ExitCritical(critical_entered);

    RISE_LOG_I("[RISE] saved journal=%lu count=%lu total=%lu pending=%lu",
               (unsigned long)s_journal_sequence,
               (unsigned long)snapshot.rise_count,
               (unsigned long)snapshot.rise_total_ms,
               (unsigned long)snapshot.pending_count);
    return 1U;
}

static uint8_t MaintenanceLedger_FlushOnce(void)
{
    app_maintenance_snapshot_t snapshot;
    uint32_t revision;
    uint8_t critical_entered;
    TaskHandle_t command_waiter = NULL;

    critical_entered = RiseCounter_EnterCritical();
    if (s_maintenance_flush_needed == 0U)
    {
        RiseCounter_ExitCritical(critical_entered);
        return 1U;
    }
    MaintenanceLedger_CopySnapshotNoLock(&snapshot);
    revision = s_maintenance_state_revision;
    RiseCounter_ExitCritical(critical_entered);

    if (MaintenanceLedger_Append(&snapshot) == 0U)
    {
        return 0U;
    }

    critical_entered = RiseCounter_EnterCritical();
    s_maintenance_persisted_revision = revision;
    if (s_maintenance_state_revision == revision)
    {
        s_maintenance_flush_needed = 0U;
    }
    if ((s_maintenance_command_waiter != NULL) &&
        (s_maintenance_persisted_revision >= s_maintenance_command_target_revision))
    {
        command_waiter = s_maintenance_command_waiter;
        s_maintenance_command_waiter = NULL;
    }
    RiseCounter_ExitCritical(critical_entered);
    if (command_waiter != NULL)
    {
        (void)xTaskNotifyGive(command_waiter);
    }
    return 1U;
}

static uint8_t MaintenanceLedger_WaitForCommit(uint32_t target_revision)
{
    uint8_t critical_entered;
    uint8_t committed;
    TaskHandle_t current_task;

    if ((s_store_task_handle == NULL) ||
        (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED))
    {
        return 0U;
    }

    critical_entered = RiseCounter_EnterCritical();
    if (s_maintenance_persisted_revision >= target_revision)
    {
        RiseCounter_ExitCritical(critical_entered);
        return 1U;
    }
    if (s_maintenance_command_waiter != NULL)
    {
        RiseCounter_ExitCritical(critical_entered);
        return 0U;
    }
    current_task = xTaskGetCurrentTaskHandle();
    s_maintenance_command_waiter = current_task;
    s_maintenance_command_target_revision = target_revision;
    RiseCounter_ExitCritical(critical_entered);

    RiseCounter_NotifyStoreTask();
    committed = (ulTaskNotifyTake(pdTRUE,
                                  pdMS_TO_TICKS(APP_MAINTENANCE_COMMAND_TIMEOUT_MS)) != 0U) ? 1U : 0U;
    if (committed == 0U)
    {
        critical_entered = RiseCounter_EnterCritical();
        if ((s_maintenance_command_waiter == current_task) &&
            (s_maintenance_command_target_revision == target_revision))
        {
            s_maintenance_command_waiter = NULL;
        }
        RiseCounter_ExitCritical(critical_entered);
    }
    return committed;
}

void App_RiseCounter_Init(void)
{
    s_rise_total_ms = 0U;
    s_rise_count = 0U;
    s_rise_remainder_ms = 0U;
    s_pending_count = 0U;
    s_upload_seq = 0U;
    s_pending_upload_epoch = 0U;
    s_state_revision = 0U;
    s_persisted_revision = 0U;
    s_flush_needed = 0U;
    s_local_changes_before_load = 0U;
    memset(&s_maintenance_snapshot, 0, sizeof(s_maintenance_snapshot));
    s_maintenance_state_revision = 0U;
    s_maintenance_persisted_revision = 0U;
    s_maintenance_flush_needed = 0U;
    s_poll_started = 0U;
    s_was_rising = 0U;
    s_last_poll_tick = 0U;
    s_journal_ready = 0U;
    memset(&s_persisted_snapshot, 0, sizeof(s_persisted_snapshot));

    s_journal_sequence = 0U;
    s_next_record_slot = 0U;
    s_maintenance_ledger_ready = 0U;
    s_maintenance_ledger_sequence = 0U;
    s_maintenance_next_record_slot = 0U;
    s_maintenance_command_waiter = NULL;
    s_maintenance_command_target_revision = 0U;

    if (RiseJournal_Load(0U) == 0U)
    {
        RISE_LOG_W("[RISE] W25Q recovery deferred; journal worker will retry");
    }
    if (MaintenanceLedger_Load(0U) == 0U)
    {
        RISE_LOG_W("[RISE] maintenance recovery deferred; journal worker will retry");
    }
}

void App_RiseCounter_Task_Create(void)
{
    BaseType_t result;

    if (s_store_task_handle != NULL)
    {
        return;
    }

    result = xTaskCreate(App_RiseCounter_StoreTask,
                         "rise_store",
                         APP_RISE_STORE_TASK_STACK_WORDS,
                         NULL,
                         APP_RISE_STORE_TASK_PRIORITY,
                         &s_store_task_handle);
    if (result != pdPASS)
    {
        s_store_task_handle = NULL;
        RISE_LOG_E("[RISE] store task create failed");
        return;
    }

    RISE_LOG_I("[RISE] store task created");
    if (RiseCounter_IsFlushNeeded() != 0U)
    {
        RiseCounter_NotifyStoreTask();
    }
}

void App_RiseCounter_Poll(lift_state_t state, uint32_t now)
{
    uint32_t elapsed_ms;
    uint32_t whole_count;
    uint32_t remainder_add;
    uint32_t remainder_sum;
    uint8_t request_flush = 0U;
    uint8_t rising_now = (state == LIFT_STATE_RISING) ? 1U : 0U;
    uint8_t critical_entered;

    critical_entered = RiseCounter_EnterCritical();
    if (s_poll_started == 0U)
    {
        s_poll_started = 1U;
        s_was_rising = rising_now;
        s_last_poll_tick = now;
        RiseCounter_ExitCritical(critical_entered);
        return;
    }

    elapsed_ms = now - s_last_poll_tick;
    if (s_was_rising != 0U)
    {
        whole_count = elapsed_ms / APP_RISE_COUNTER_PERIOD_MS;
        remainder_add = elapsed_ms % APP_RISE_COUNTER_PERIOD_MS;
        remainder_sum = s_rise_remainder_ms + remainder_add;
        if (remainder_sum >= APP_RISE_COUNTER_PERIOD_MS)
        {
            whole_count++;
            remainder_sum -= APP_RISE_COUNTER_PERIOD_MS;
        }

        s_rise_total_ms += elapsed_ms;
        s_rise_remainder_ms = remainder_sum;

        if (whole_count != 0U)
        {
            s_rise_count += whole_count;
            s_pending_count += whole_count;
            s_upload_seq += whole_count;
            s_maintenance_snapshot.total_lift_count += whole_count;
            s_maintenance_snapshot.maintenance_lift_count += whole_count;
            if (s_maintenance_snapshot.maintenance_lift_count >= APP_MAINTENANCE_THRESHOLD)
            {
                s_maintenance_snapshot.maintenance_due = 1U;
            }
            g_stats.up_count = s_rise_count;
            g_stats.up_count_main = s_rise_count;
            RiseCounter_RequestFlushNoLock();
            MaintenanceLedger_RequestFlushNoLock();
            request_flush = 1U;
        }
    }

    if ((s_was_rising != 0U) && (rising_now == 0U))
    {
        /* Preserve sub-three-second carry and total rising time on every stop. */
        RiseCounter_RequestFlushNoLock();
        request_flush = 1U;
    }

    s_was_rising = rising_now;
    s_last_poll_tick = now;
    RiseCounter_ExitCritical(critical_entered);

    if (request_flush != 0U)
    {
        RiseCounter_NotifyStoreTask();
    }
}

void App_RiseCounter_GetSnapshot(app_rise_counter_snapshot_t *out)
{
    uint8_t critical_entered;

    if (out == NULL)
    {
        return;
    }

    critical_entered = RiseCounter_EnterCritical();
    RiseCounter_CopySnapshotNoLock(out);
    RiseCounter_ExitCritical(critical_entered);
}

void App_RiseCounter_GetMaintenanceSnapshot(app_maintenance_snapshot_t *out)
{
    uint8_t critical_entered;

    if (out == NULL)
    {
        return;
    }

    critical_entered = RiseCounter_EnterCritical();
    MaintenanceLedger_CopySnapshotNoLock(out);
    RiseCounter_ExitCritical(critical_entered);
}

uint8_t App_RiseCounter_MaintenanceDone(const char *msg_id)
{
    uint8_t critical_entered;
    uint32_t target_revision;
    uint32_t command_hash = MaintenanceLedger_CommandHash(APP_MAINTENANCE_COMMAND_DONE, msg_id);

    if ((command_hash == 0U) ||
        (MaintenanceLedger_CommandSeen(APP_MAINTENANCE_COMMAND_DONE, command_hash) != 0U))
    {
        return (command_hash != 0U) ? 1U : 0U;
    }
    critical_entered = RiseCounter_EnterCritical();
    if (s_maintenance_ledger_ready == 0U)
    {
        RiseCounter_ExitCritical(critical_entered);
        return 0U;
    }

    s_maintenance_snapshot.last_maintenance_total =
        s_maintenance_snapshot.total_lift_count;
    s_maintenance_snapshot.maintenance_lift_count = 0U;
    s_maintenance_snapshot.maintenance_count++;
    s_maintenance_snapshot.maintenance_due = 0U;
    s_maintenance_snapshot.last_command_hash = command_hash;
    s_maintenance_snapshot.last_command_type = APP_MAINTENANCE_COMMAND_DONE;
    MaintenanceLedger_RequestFlushNoLock();
    target_revision = s_maintenance_state_revision;
    RiseCounter_ExitCritical(critical_entered);
    return MaintenanceLedger_WaitForCommit(target_revision);
}

uint8_t App_RiseCounter_ResetUsage(const char *msg_id)
{
    uint8_t critical_entered;
    uint32_t next_epoch;
    uint32_t target_revision;
    uint32_t command_hash = MaintenanceLedger_CommandHash(APP_MAINTENANCE_COMMAND_RESET, msg_id);

    if ((command_hash == 0U) ||
        (MaintenanceLedger_CommandSeen(APP_MAINTENANCE_COMMAND_RESET, command_hash) != 0U))
    {
        return (command_hash != 0U) ? 1U : 0U;
    }
    critical_entered = RiseCounter_EnterCritical();
    if (s_maintenance_ledger_ready == 0U)
    {
        RiseCounter_ExitCritical(critical_entered);
        return 0U;
    }

    next_epoch = s_maintenance_snapshot.usage_epoch + 1U;
    s_rise_total_ms = 0U;
    s_rise_count = 0U;
    s_rise_remainder_ms = 0U;
    s_pending_count = 0U;
    s_upload_seq = 0U;
    memset(&s_maintenance_snapshot, 0, sizeof(s_maintenance_snapshot));
    s_maintenance_snapshot.usage_epoch = next_epoch;
    s_maintenance_snapshot.last_command_hash = command_hash;
    s_maintenance_snapshot.last_command_type = APP_MAINTENANCE_COMMAND_RESET;
    g_stats.up_count = 0U;
    g_stats.up_count_main = 0U;
    RiseCounter_RequestFlushNoLock();
    MaintenanceLedger_RequestFlushNoLock();
    target_revision = s_maintenance_state_revision;
    RiseCounter_ExitCritical(critical_entered);
    RiseCounter_NotifyStoreTask();
    return MaintenanceLedger_WaitForCommit(target_revision);
}

uint8_t App_RiseCounter_GetPendingUpload(app_rise_counter_upload_t *out)
{
    uint8_t has_pending = 0U;
    uint8_t critical_entered;

    if (out == NULL)
    {
        return 0U;
    }

    critical_entered = RiseCounter_EnterCritical();
    if ((s_pending_count != 0U) &&
        (s_persisted_revision == s_state_revision))
    {
        /* upload_seq is the highest allocated count sequence. The oldest
         * pending sequence is derived so concurrent new counts stay ordered.
         * Return the verified journal snapshot, never a newer RAM-only value. */
        out->seq = s_persisted_snapshot.upload_seq -
                   s_persisted_snapshot.pending_count + 1U;
        out->delta = s_persisted_snapshot.pending_count;
        out->total_count = s_persisted_snapshot.rise_count;
        out->total_rise_ms = s_persisted_snapshot.rise_total_ms;
        out->remainder_ms = s_persisted_snapshot.rise_remainder_ms;
        out->usage_epoch = s_maintenance_snapshot.usage_epoch;
        s_pending_upload_epoch = s_maintenance_snapshot.usage_epoch;
        has_pending = 1U;
    }
    RiseCounter_ExitCritical(critical_entered);

    return has_pending;
}

uint8_t App_RiseCounter_MarkUploadSent(uint32_t seq, uint32_t delta)
{
    uint32_t expected_seq;
    uint8_t accepted = 0U;
    uint8_t critical_entered;

    if (delta == 0U)
    {
        return 0U;
    }

    critical_entered = RiseCounter_EnterCritical();
    if ((s_pending_count != 0U) &&
        (s_pending_upload_epoch == s_maintenance_snapshot.usage_epoch))
    {
        expected_seq = s_upload_seq - s_pending_count + 1U;
        if ((seq == expected_seq) && (delta <= s_pending_count))
        {
            s_pending_count -= delta;
            RiseCounter_RequestFlushNoLock();
            accepted = 1U;
        }
    }
    RiseCounter_ExitCritical(critical_entered);

    if (accepted != 0U)
    {
        RiseCounter_NotifyStoreTask();
    }
    else
    {
        RISE_LOG_W("[RISE] rejected upload ack seq=%lu delta=%lu",
                   (unsigned long)seq,
                   (unsigned long)delta);
    }

    return accepted;
}

static void App_RiseCounter_StoreTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        if (RiseCounter_IsFlushNeeded() != 0U)
        {
            if ((s_journal_ready == 0U) && (RiseJournal_Load(1U) == 0U))
            {
                vTaskDelay(pdMS_TO_TICKS(APP_RISE_STORE_RETRY_MS));
                continue;
            }

            if ((s_maintenance_ledger_ready == 0U) &&
                (MaintenanceLedger_Load(1U) == 0U))
            {
                vTaskDelay(pdMS_TO_TICKS(APP_RISE_STORE_RETRY_MS));
                continue;
            }

            if ((MaintenanceLedger_FlushOnce() == 0U) ||
                (RiseCounter_FlushOnce() == 0U))
            {
                vTaskDelay(pdMS_TO_TICKS(APP_RISE_STORE_RETRY_MS));
            }
            continue;
        }

        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

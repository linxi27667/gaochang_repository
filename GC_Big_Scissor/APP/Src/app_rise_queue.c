#include "app_rise_queue.h"
#include "app_maintenance.h"
#include "app_w25qxx.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#include <string.h>

#define RISE_QUEUE_MAGIC                 0x55504351UL
#define RISE_QUEUE_VERSION               2U
#define RISE_QUEUE_TYPE_COUNT            1U
#define RISE_QUEUE_TYPE_CHECKPOINT       2U
#define RISE_QUEUE_ROLE_NONE             0xFFU
#define RISE_QUEUE_STATE_EMPTY           0xFFU
#define RISE_QUEUE_STATE_PENDING         0x7FU
#define RISE_QUEUE_STATE_ACKED           0x3FU
#define RISE_QUEUE_RAM_CAPACITY          8U
#define RISE_QUEUE_INVALID_ADDRESS       0xFFFFFFFFUL
#define RISE_QUEUE_PERIOD_MS             3000U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t type;
    uint8_t role;
    uint32_t sequence;
    uint32_t up_total;
    uint32_t up_main;
    uint32_t up_sub;
    uint16_t main_remainder_ms;
    uint16_t sub_remainder_ms;
    uint32_t crc;
    uint8_t state;
    uint8_t usage_epoch[4];
    uint8_t reserved[3];
} rise_queue_record_t;

typedef char rise_queue_record_size_must_be_40[
    (sizeof(rise_queue_record_t) == 40U) ? 1 : -1];

#define RISE_QUEUE_RECORDS_PER_SECTOR \
    (W25Q_RISE_QUEUE_SECTOR_SIZE / sizeof(rise_queue_record_t))

static w25q_rise_event_t s_ram_queue[RISE_QUEUE_RAM_CAPACITY];
static uint8_t s_ram_head;
static uint8_t s_ram_count;
static uint8_t s_overflow;
static uint8_t s_flash_ready;
static uint8_t s_remainder_dirty;
static uint32_t s_remainder_generation;
static uint32_t s_remainder_ms[2];
static uint32_t s_usage_epoch;
static uint32_t s_next_sequence = 1U;
static uint16_t s_flash_pending_count;
static uint32_t s_flash_head_address = RISE_QUEUE_INVALID_ADDRESS;
static uint32_t s_write_address = W25Q_RISE_QUEUE_ADDR;

static uint8_t RiseQueue_RoleIndex(lift_role_t role)
{
    return (role == LIFT_ROLE_MAIN) ? 0U : 1U;
}

static uint32_t RiseQueue_EndAddress(void)
{
    return W25Q_RISE_QUEUE_ADDR +
           (W25Q_RISE_QUEUE_SECTOR_SIZE * W25Q_RISE_QUEUE_SECTOR_COUNT);
}

static uint32_t RiseQueue_SectorAddress(uint32_t address)
{
    return address - ((address - W25Q_RISE_QUEUE_ADDR) % W25Q_RISE_QUEUE_SECTOR_SIZE);
}

static uint32_t RiseQueue_NextAddress(uint32_t address)
{
    uint32_t next = address + sizeof(rise_queue_record_t);
    uint32_t sector_end = RiseQueue_SectorAddress(address) + W25Q_RISE_QUEUE_SECTOR_SIZE;

    if ((next + sizeof(rise_queue_record_t)) > sector_end)
    {
        next = sector_end;
    }

    if (next >= RiseQueue_EndAddress())
    {
        next = W25Q_RISE_QUEUE_ADDR;
    }

    return next;
}

static uint32_t RiseQueue_CrcUpdate(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t bit;

    while (length-- > 0U)
    {
        crc ^= *data++;
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }

    return crc;
}

static uint32_t RiseQueue_RecordCrc(const rise_queue_record_t *record)
{
    return ~RiseQueue_CrcUpdate(0xFFFFFFFFUL,
                                (const uint8_t *)record,
                                (uint32_t)offsetof(rise_queue_record_t, crc));
}

static uint8_t RiseQueue_IsBlank(const rise_queue_record_t *record)
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

static uint8_t RiseQueue_IsValid(const rise_queue_record_t *record)
{
    if ((record->magic != RISE_QUEUE_MAGIC) ||
        ((record->version != 1U) && (record->version != RISE_QUEUE_VERSION)) ||
        (record->type != RISE_QUEUE_TYPE_COUNT &&
         record->type != RISE_QUEUE_TYPE_CHECKPOINT) ||
        (record->state != RISE_QUEUE_STATE_PENDING &&
         record->state != RISE_QUEUE_STATE_ACKED))
    {
        return 0U;
    }

    return (record->crc == RiseQueue_RecordCrc(record)) ? 1U : 0U;
}

static uint32_t RiseQueue_GetRecordUsageEpoch(const rise_queue_record_t *record)
{
    return ((uint32_t)record->usage_epoch[0]) |
           ((uint32_t)record->usage_epoch[1] << 8U) |
           ((uint32_t)record->usage_epoch[2] << 16U) |
           ((uint32_t)record->usage_epoch[3] << 24U);
}

static void RiseQueue_SetRecordUsageEpoch(rise_queue_record_t *record,
                                          uint32_t usage_epoch)
{
    record->usage_epoch[0] = (uint8_t)usage_epoch;
    record->usage_epoch[1] = (uint8_t)(usage_epoch >> 8U);
    record->usage_epoch[2] = (uint8_t)(usage_epoch >> 16U);
    record->usage_epoch[3] = (uint8_t)(usage_epoch >> 24U);
}

static uint8_t RiseQueue_IsPending(const rise_queue_record_t *record)
{
    return ((RiseQueue_IsValid(record) != 0U) &&
            (record->type == RISE_QUEUE_TYPE_COUNT) &&
            (((record->version == 1U) ? 0U : RiseQueue_GetRecordUsageEpoch(record)) == s_usage_epoch) &&
            (record->state == RISE_QUEUE_STATE_PENDING)) ? 1U : 0U;
}

static uint8_t RiseQueue_Read(uint32_t address, rise_queue_record_t *record)
{
    return W25Q_Read_Buffer(&W25Q_Flash,
                            address,
                            (uint8_t *)record,
                            sizeof(*record));
}

static uint8_t RiseQueue_SectorHasPending(uint32_t sector_address)
{
    uint32_t index;
    rise_queue_record_t record;

    for (index = 0U; index < RISE_QUEUE_RECORDS_PER_SECTOR; ++index)
    {
        uint32_t address = sector_address + (index * sizeof(record));

        if (RiseQueue_Read(address, &record) != W25Q_OK)
        {
            return 1U;
        }

        if (RiseQueue_IsPending(&record) != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t RiseQueue_PrepareWriteSlot(void)
{
    uint32_t attempt;
    rise_queue_record_t record;

    for (attempt = 0U;
         attempt < (W25Q_RISE_QUEUE_SECTOR_COUNT + 1U);
         ++attempt)
    {
        uint32_t sector_address = RiseQueue_SectorAddress(s_write_address);

        if (RiseQueue_Read(s_write_address, &record) != W25Q_OK)
        {
            return W25Q_ERR;
        }

        if (RiseQueue_IsBlank(&record) != 0U)
        {
            return W25Q_OK;
        }

        if (RiseQueue_SectorHasPending(sector_address) == 0U)
        {
            if (W25Q_Sector_Erase(&W25Q_Flash, sector_address) != W25Q_OK)
            {
                return W25Q_ERR;
            }

            s_write_address = sector_address;
            return W25Q_OK;
        }

        s_write_address = sector_address + W25Q_RISE_QUEUE_SECTOR_SIZE;
        if (s_write_address >= RiseQueue_EndAddress())
        {
            s_write_address = W25Q_RISE_QUEUE_ADDR;
        }
    }

    return W25Q_ERR;
}

static uint8_t RiseQueue_Append(rise_queue_record_t *record, uint8_t pending)
{
    uint8_t state;
    uint32_t state_address;

    if ((s_flash_ready == 0U) || (RiseQueue_PrepareWriteSlot() != W25Q_OK))
    {
        return W25Q_ERR;
    }

    record->magic = RISE_QUEUE_MAGIC;
    record->version = RISE_QUEUE_VERSION;
    record->crc = RiseQueue_RecordCrc(record);
    record->state = RISE_QUEUE_STATE_EMPTY;

    if (W25Q_Write_MultiPage(&W25Q_Flash,
                             s_write_address,
                             (const uint8_t *)record,
                             sizeof(*record)) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    state = (pending != 0U) ? RISE_QUEUE_STATE_PENDING : RISE_QUEUE_STATE_ACKED;
    state_address = s_write_address + offsetof(rise_queue_record_t, state);
    if (W25Q_Page_Program(&W25Q_Flash, state_address, &state, 1U) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    if (pending != 0U)
    {
        if (s_flash_pending_count == 0U)
        {
            s_flash_head_address = s_write_address;
        }
        s_flash_pending_count++;
    }

    s_write_address = RiseQueue_NextAddress(s_write_address);
    return W25Q_OK;
}

static void RiseQueue_BuildCountRecord(rise_queue_record_t *record,
                                       const w25q_rise_event_t *event,
                                       uint16_t main_remainder_ms,
                                       uint16_t sub_remainder_ms)
{
    memset(record, 0xFF, sizeof(*record));
    record->type = RISE_QUEUE_TYPE_COUNT;
    record->role = (uint8_t)event->role;
    record->sequence = event->sequence;
    record->up_total = event->up_total;
    record->up_main = event->up_main;
    record->up_sub = event->up_sub;
    RiseQueue_SetRecordUsageEpoch(record, event->usage_epoch);
    record->main_remainder_ms = main_remainder_ms;
    record->sub_remainder_ms = sub_remainder_ms;
}

static void RiseQueue_ToEvent(const rise_queue_record_t *record,
                              w25q_rise_event_t *event)
{
    event->sequence = record->sequence;
    event->up_total = record->up_total;
    event->up_main = record->up_main;
    event->up_sub = record->up_sub;
    event->usage_epoch = (record->version == 1U) ? 0U : RiseQueue_GetRecordUsageEpoch(record);
    event->role = (record->role == (uint8_t)LIFT_ROLE_MAIN) ?
                  LIFT_ROLE_MAIN : LIFT_ROLE_SUB;
    event->from_flash = 1U;
}

static void RiseQueue_FindOldestPending(void)
{
    uint32_t sector_index;
    uint32_t oldest_sequence = 0U;
    uint32_t oldest_address = RISE_QUEUE_INVALID_ADDRESS;
    uint16_t pending_count = 0U;
    rise_queue_record_t record;

    for (sector_index = 0U;
         sector_index < W25Q_RISE_QUEUE_SECTOR_COUNT;
         ++sector_index)
    {
        uint32_t entry_index;
        uint32_t sector_address = W25Q_RISE_QUEUE_ADDR +
                                  (sector_index * W25Q_RISE_QUEUE_SECTOR_SIZE);

        for (entry_index = 0U;
             entry_index < RISE_QUEUE_RECORDS_PER_SECTOR;
             ++entry_index)
        {
            uint32_t address = sector_address + (entry_index * sizeof(record));

            if ((RiseQueue_Read(address, &record) == W25Q_OK) &&
                (RiseQueue_IsPending(&record) != 0U))
            {
                if ((oldest_address == RISE_QUEUE_INVALID_ADDRESS) ||
                    (record.sequence < oldest_sequence))
                {
                    oldest_sequence = record.sequence;
                    oldest_address = address;
                }
                pending_count++;
            }
        }
    }

    s_flash_pending_count = pending_count;
    s_flash_head_address = oldest_address;
}

void App_RiseQueue_Init(void)
{
    uint32_t sector_index;
    uint32_t max_sequence = 0U;
    uint32_t max_sequence_address = RISE_QUEUE_INVALID_ADDRESS;
    uint32_t latest_sequence = 0U;
    rise_queue_record_t latest_record;
    rise_queue_record_t record;

    memset(s_ram_queue, 0, sizeof(s_ram_queue));
    memset(s_remainder_ms, 0, sizeof(s_remainder_ms));
    memset(&latest_record, 0, sizeof(latest_record));
    s_ram_head = 0U;
    s_ram_count = 0U;
    s_overflow = 0U;
    s_remainder_dirty = 0U;
    s_remainder_generation = 0U;
    s_usage_epoch = 0U;
    s_flash_pending_count = 0U;
    s_flash_head_address = RISE_QUEUE_INVALID_ADDRESS;
    s_write_address = W25Q_RISE_QUEUE_ADDR;
    s_next_sequence = 1U;
    s_flash_ready = 1U;

    {
        app_maintenance_status_t maintenance;
        App_Maintenance_GetStatus(&maintenance);
        s_usage_epoch = maintenance.usage_epoch;
    }

    for (sector_index = 0U;
         sector_index < W25Q_RISE_QUEUE_SECTOR_COUNT;
         ++sector_index)
    {
        uint32_t entry_index;
        uint32_t sector_address = W25Q_RISE_QUEUE_ADDR +
                                  (sector_index * W25Q_RISE_QUEUE_SECTOR_SIZE);

        for (entry_index = 0U;
             entry_index < RISE_QUEUE_RECORDS_PER_SECTOR;
             ++entry_index)
        {
            uint32_t address = sector_address + (entry_index * sizeof(record));

            if ((RiseQueue_Read(address, &record) != W25Q_OK) ||
                (RiseQueue_IsValid(&record) == 0U))
            {
                continue;
            }

            if ((max_sequence_address == RISE_QUEUE_INVALID_ADDRESS) ||
                (record.sequence > max_sequence))
            {
                max_sequence = record.sequence;
                max_sequence_address = address;
            }

            if ((((record.version == 1U) ? 0U : RiseQueue_GetRecordUsageEpoch(&record)) == s_usage_epoch) &&
                ((latest_sequence == 0U) || (record.sequence > latest_sequence)))
            {
                latest_sequence = record.sequence;
                latest_record = record;
            }
        }
    }

    if (max_sequence_address != RISE_QUEUE_INVALID_ADDRESS)
    {
        s_write_address = RiseQueue_NextAddress(max_sequence_address);
        s_next_sequence = max_sequence + 1U;
        if (s_next_sequence == 0U)
        {
            s_next_sequence = 1U;
        }

        if (latest_sequence != 0U)
        {
            g_stats.up_count = latest_record.up_total;
            g_stats.up_count_main = latest_record.up_main;
            g_stats.up_count_sub = latest_record.up_sub;
            s_remainder_ms[0] = latest_record.main_remainder_ms;
            s_remainder_ms[1] = latest_record.sub_remainder_ms;
        }
    }

    RiseQueue_FindOldestPending();
}

void App_RiseQueue_Enqueue(lift_role_t role,
                           uint32_t up_total,
                           uint32_t up_main,
                           uint32_t up_sub,
                           uint32_t usage_epoch)
{
    uint8_t tail;
    w25q_rise_event_t event;

    taskENTER_CRITICAL();
    if (s_ram_count >= RISE_QUEUE_RAM_CAPACITY)
    {
        s_overflow = 1U;
        taskEXIT_CRITICAL();
        return;
    }

    tail = (uint8_t)((s_ram_head + s_ram_count) % RISE_QUEUE_RAM_CAPACITY);
    event.sequence = s_next_sequence++;
    if (s_next_sequence == 0U)
    {
        s_next_sequence = 1U;
    }
    event.up_total = up_total;
    event.up_main = up_main;
    event.up_sub = up_sub;
    event.usage_epoch = usage_epoch;
    event.role = role;
    event.from_flash = 0U;
    s_ram_queue[tail] = event;
    s_ram_count++;
    taskEXIT_CRITICAL();
}

uint8_t App_RiseQueue_ResetUsage(uint32_t usage_epoch)
{
    uint32_t sector_index;

    /* Stop old RAM records immediately; erasure below is only physical cleanup. */
    taskENTER_CRITICAL();
    memset(s_ram_queue, 0, sizeof(s_ram_queue));
    memset(s_remainder_ms, 0, sizeof(s_remainder_ms));
    s_ram_head = 0U;
    s_ram_count = 0U;
    s_overflow = 0U;
    s_remainder_dirty = 0U;
    s_remainder_generation++;
    s_flash_pending_count = 0U;
    s_flash_head_address = RISE_QUEUE_INVALID_ADDRESS;
    s_write_address = W25Q_RISE_QUEUE_ADDR;
    s_next_sequence = 1U;
    s_usage_epoch = usage_epoch;
    taskEXIT_CRITICAL();

    for (sector_index = 0U;
         sector_index < W25Q_RISE_QUEUE_SECTOR_COUNT;
         ++sector_index)
    {
        if (W25Q_Sector_Erase(&W25Q_Flash,
                              W25Q_RISE_QUEUE_ADDR +
                              (sector_index * W25Q_RISE_QUEUE_SECTOR_SIZE)) != W25Q_OK)
        {
            return W25Q_ERR;
        }
    }

    return W25Q_OK;
}

uint32_t App_RiseQueue_GetRemainder(lift_role_t role)
{
    uint32_t remainder;

    taskENTER_CRITICAL();
    remainder = s_remainder_ms[RiseQueue_RoleIndex(role)];
    taskEXIT_CRITICAL();

    return remainder;
}

void App_RiseQueue_SetRemainder(lift_role_t role, uint32_t remainder_ms)
{
    uint8_t index = RiseQueue_RoleIndex(role);

    if (remainder_ms >= RISE_QUEUE_PERIOD_MS)
    {
        remainder_ms %= RISE_QUEUE_PERIOD_MS;
    }

    taskENTER_CRITICAL();
    if (s_remainder_ms[index] != remainder_ms)
    {
        s_remainder_ms[index] = remainder_ms;
        s_remainder_dirty = 1U;
        s_remainder_generation++;
    }
    taskEXIT_CRITICAL();
}

uint8_t App_W25Qxx_RiseQueue_PersistPending(void)
{
    w25q_rise_event_t event;
    rise_queue_record_t record;

    if (s_flash_ready == 0U)
    {
        return W25Q_ERR;
    }

    while (1)
    {
        uint16_t main_remainder_ms;
        uint16_t sub_remainder_ms;

        taskENTER_CRITICAL();
        if (s_ram_count == 0U)
        {
            taskEXIT_CRITICAL();
            break;
        }
        event = s_ram_queue[s_ram_head];
        main_remainder_ms = (uint16_t)s_remainder_ms[0];
        sub_remainder_ms = (uint16_t)s_remainder_ms[1];
        taskEXIT_CRITICAL();

        RiseQueue_BuildCountRecord(&record,
                                   &event,
                                   main_remainder_ms,
                                   sub_remainder_ms);
        if (RiseQueue_Append(&record, 1U) != W25Q_OK)
        {
            taskENTER_CRITICAL();
            s_overflow = 1U;
            taskEXIT_CRITICAL();
            return W25Q_ERR;
        }

        taskENTER_CRITICAL();
        if ((s_ram_count != 0U) &&
            (s_ram_queue[s_ram_head].sequence == event.sequence))
        {
            s_ram_head = (uint8_t)((s_ram_head + 1U) % RISE_QUEUE_RAM_CAPACITY);
            s_ram_count--;
        }
        taskEXIT_CRITICAL();
    }

    taskENTER_CRITICAL();
    if (s_remainder_dirty != 0U)
    {
        uint32_t generation = s_remainder_generation;

        memset(&record, 0xFF, sizeof(record));
        record.type = RISE_QUEUE_TYPE_CHECKPOINT;
        record.role = RISE_QUEUE_ROLE_NONE;
        record.sequence = s_next_sequence++;
        if (s_next_sequence == 0U)
        {
            s_next_sequence = 1U;
        }
        record.up_total = g_stats.up_count;
        record.up_main = g_stats.up_count_main;
        record.up_sub = g_stats.up_count_sub;
        RiseQueue_SetRecordUsageEpoch(&record, s_usage_epoch);
        record.main_remainder_ms = (uint16_t)s_remainder_ms[0];
        record.sub_remainder_ms = (uint16_t)s_remainder_ms[1];
        taskEXIT_CRITICAL();

        if (RiseQueue_Append(&record, 0U) != W25Q_OK)
        {
            taskENTER_CRITICAL();
            s_overflow = 1U;
            taskEXIT_CRITICAL();
            return W25Q_ERR;
        }

        taskENTER_CRITICAL();
        if (s_remainder_generation == generation)
        {
            s_remainder_dirty = 0U;
        }
        taskEXIT_CRITICAL();
    }
    else
    {
        taskEXIT_CRITICAL();
    }

    return W25Q_OK;
}

uint8_t App_W25Qxx_RiseQueue_HasFlashPending(void)
{
    return (s_flash_pending_count != 0U) ? 1U : 0U;
}

uint8_t App_W25Qxx_RiseQueue_Peek(w25q_rise_event_t *out)
{
    rise_queue_record_t record;

    if (out == NULL)
    {
        return 0U;
    }

    if (s_flash_pending_count != 0U)
    {
        if ((s_flash_head_address == RISE_QUEUE_INVALID_ADDRESS) ||
            (RiseQueue_Read(s_flash_head_address, &record) != W25Q_OK) ||
            (RiseQueue_IsPending(&record) == 0U))
        {
            RiseQueue_FindOldestPending();
            if ((s_flash_head_address == RISE_QUEUE_INVALID_ADDRESS) ||
                (RiseQueue_Read(s_flash_head_address, &record) != W25Q_OK) ||
                (RiseQueue_IsPending(&record) == 0U))
            {
                return 0U;
            }
        }

        RiseQueue_ToEvent(&record, out);
        return 1U;
    }

    taskENTER_CRITICAL();
    if (s_ram_count != 0U)
    {
        *out = s_ram_queue[s_ram_head];
        taskEXIT_CRITICAL();
        return 1U;
    }
    taskEXIT_CRITICAL();

    return 0U;
}

uint8_t App_W25Qxx_RiseQueue_Ack(const w25q_rise_event_t *event)
{
    uint8_t state = RISE_QUEUE_STATE_ACKED;

    if (event == NULL)
    {
        return W25Q_ERR;
    }

    if (event->from_flash == 0U)
    {
        taskENTER_CRITICAL();
        if ((s_ram_count == 0U) ||
            (s_ram_queue[s_ram_head].sequence != event->sequence))
        {
            taskEXIT_CRITICAL();
            return W25Q_ERR;
        }
        s_ram_head = (uint8_t)((s_ram_head + 1U) % RISE_QUEUE_RAM_CAPACITY);
        s_ram_count--;
        taskEXIT_CRITICAL();
        return W25Q_OK;
    }

    if ((s_flash_head_address == RISE_QUEUE_INVALID_ADDRESS) ||
        (W25Q_Page_Program(&W25Q_Flash,
                            s_flash_head_address + offsetof(rise_queue_record_t, state),
                            &state,
                            1U) != W25Q_OK))
    {
        return W25Q_ERR;
    }

    RiseQueue_FindOldestPending();
    return W25Q_OK;
}

uint8_t App_W25Qxx_RiseQueue_HasOverflow(void)
{
    uint8_t overflow;

    taskENTER_CRITICAL();
    overflow = s_overflow;
    taskEXIT_CRITICAL();

    return overflow;
}

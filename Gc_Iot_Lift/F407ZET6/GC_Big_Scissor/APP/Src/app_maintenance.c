#include "app_maintenance.h"

#include "app_w25qxx.h"

#include <stddef.h>
#include <string.h>

#define MAINTENANCE_MAGIC             0x4D41494EU
#define MAINTENANCE_VERSION           1U
#define MAINTENANCE_COMMIT_EMPTY      0xFFU
#define MAINTENANCE_COMMIT_DONE       0x3FU
#define MAINTENANCE_INVALID_ADDRESS   0xFFFFFFFFUL

#define MAINTENANCE_RECORDS_PER_SECTOR \
    (W25Q_MAINTENANCE_SECTOR_SIZE / sizeof(maintenance_record_t))

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    uint32_t sequence;
    uint32_t total_lift_count;
    uint32_t maintenance_lift_count;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint8_t maintenance_due;
    uint8_t reserved1[3];
    uint32_t usage_epoch;
    uint32_t crc32;
    uint8_t commit;
    uint8_t reserved2[3];
} maintenance_record_t;

typedef char maintenance_record_size_must_be_44[
    (sizeof(maintenance_record_t) == 44U) ? 1 : -1];

static app_maintenance_status_t s_status;
static uint32_t s_next_sequence = 1U;
static uint32_t s_write_address = W25Q_MAINTENANCE_ADDR;
static uint8_t s_initialized;
static uint32_t s_last_command_hash;
static uint8_t s_last_command_type;
static uint32_t s_command_hashes[W25Q_MAINTENANCE_SECTOR_COUNT * MAINTENANCE_RECORDS_PER_SECTOR];
static uint8_t s_command_types[W25Q_MAINTENANCE_SECTOR_COUNT * MAINTENANCE_RECORDS_PER_SECTOR];
static uint32_t s_command_count;

static uint8_t Maintenance_Read(uint32_t address, maintenance_record_t *record);
static uint8_t Maintenance_IsValid(const maintenance_record_t *record);

#define MAINTENANCE_COMMAND_DONE       1U
#define MAINTENANCE_COMMAND_RESET      2U

static uint32_t Maintenance_EndAddress(void)
{
    return W25Q_MAINTENANCE_ADDR +
           (W25Q_MAINTENANCE_SECTOR_SIZE * W25Q_MAINTENANCE_SECTOR_COUNT);
}

static uint32_t Maintenance_SectorAddress(uint32_t address)
{
    return address - ((address - W25Q_MAINTENANCE_ADDR) %
                      W25Q_MAINTENANCE_SECTOR_SIZE);
}

static uint32_t Maintenance_NextAddress(uint32_t address)
{
    uint32_t sector_start = Maintenance_SectorAddress(address);
    uint32_t next = address + sizeof(maintenance_record_t);

    if ((next + sizeof(maintenance_record_t)) >
        (sector_start + W25Q_MAINTENANCE_SECTOR_SIZE))
    {
        next = sector_start + W25Q_MAINTENANCE_SECTOR_SIZE;
    }

    if (next >= Maintenance_EndAddress())
    {
        next = W25Q_MAINTENANCE_ADDR;
    }

    return next;
}

static uint32_t Maintenance_CrcUpdate(uint32_t crc,
                                      const uint8_t *data,
                                      uint32_t length)
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

static uint32_t Maintenance_RecordCrc(const maintenance_record_t *record)
{
    return ~Maintenance_CrcUpdate(0xFFFFFFFFUL,
                                  (const uint8_t *)record,
                                  (uint32_t)offsetof(maintenance_record_t, crc32));
}

static uint32_t Maintenance_CommandHash(uint8_t command, const char *msg_id)
{
    uint32_t hash;

    if ((msg_id == NULL) || (msg_id[0] == '\0'))
    {
        return 0U;
    }
    hash = ~Maintenance_CrcUpdate(0xFFFFFFFFUL,
                                  (const uint8_t *)msg_id,
                                  (uint32_t)strlen(msg_id));
    hash ^= ((uint32_t)command * 0x9E3779B9UL);
    return (hash != 0U) ? hash : 0xFFFFFFFFUL;
}

static void Maintenance_SetRecordCommand(maintenance_record_t *record)
{
    record->reserved0 = (uint16_t)(s_last_command_hash & 0xFFFFU);
    record->reserved1[0] = (uint8_t)((s_last_command_hash >> 16U) & 0xFFU);
    record->reserved1[1] = (uint8_t)((s_last_command_hash >> 24U) & 0xFFU);
    record->reserved1[2] = s_last_command_type;
}

static void Maintenance_LoadRecordCommand(const maintenance_record_t *record)
{
    s_last_command_hash = (uint32_t)record->reserved0 |
        ((uint32_t)record->reserved1[0] << 16U) |
        ((uint32_t)record->reserved1[1] << 24U);
    s_last_command_type = record->reserved1[2];
    if (s_last_command_type > MAINTENANCE_COMMAND_RESET)
    {
        s_last_command_hash = 0U;
        s_last_command_type = 0U;
    }
}

static uint8_t Maintenance_CommandSeen(uint8_t command, uint32_t command_hash)
{
    uint32_t index;

    for (index = 0U; index < s_command_count; ++index)
    {
        if ((s_command_types[index] == command) &&
            (s_command_hashes[index] == command_hash))
        {
            return 1U;
        }
    }
    return 0U;
}

static void Maintenance_RememberCommand(uint8_t command, uint32_t command_hash)
{
    if (((command != MAINTENANCE_COMMAND_DONE) &&
         (command != MAINTENANCE_COMMAND_RESET)) ||
        (command_hash == 0U) ||
        (Maintenance_CommandSeen(command, command_hash) != 0U))
    {
        return;
    }
    if (s_command_count < (W25Q_MAINTENANCE_SECTOR_COUNT * MAINTENANCE_RECORDS_PER_SECTOR))
    {
        s_command_types[s_command_count] = command;
        s_command_hashes[s_command_count] = command_hash;
        s_command_count++;
    }
}

static uint8_t Maintenance_Read(uint32_t address, maintenance_record_t *record)
{
    return W25Q_Read_Buffer(&W25Q_Flash,
                            address,
                            (uint8_t *)record,
                            sizeof(*record));
}

static uint8_t Maintenance_IsBlank(const maintenance_record_t *record)
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

static uint8_t Maintenance_IsValid(const maintenance_record_t *record)
{
    if ((record->magic != MAINTENANCE_MAGIC) ||
        (record->version != MAINTENANCE_VERSION) ||
        (record->commit != MAINTENANCE_COMMIT_DONE))
    {
        return 0U;
    }

    return (record->crc32 == Maintenance_RecordCrc(record)) ? 1U : 0U;
}

static uint8_t Maintenance_PrepareWriteSlot(void)
{
    maintenance_record_t record;
    uint32_t attempts;

    /* A programming interruption leaves a nonblank but invalid record at
     * the current slot. Never erase its sector: it still contains the last
     * committed history. Skip that slot and only recycle a sector once its
     * first committed record is reached on the next journal lap. */
    for (attempts = 0U;
         attempts < (W25Q_MAINTENANCE_SECTOR_COUNT * MAINTENANCE_RECORDS_PER_SECTOR);
         attempts++)
    {
        if (Maintenance_Read(s_write_address, &record) != W25Q_OK)
        {
            return W25Q_ERR;
        }

        if (Maintenance_IsBlank(&record) != 0U)
        {
            return W25Q_OK;
        }

        if (Maintenance_IsValid(&record) == 0U)
        {
            s_write_address = Maintenance_NextAddress(s_write_address);
            continue;
        }

        /* A valid record at the selected slot means the circular journal
         * has lapped. The sector beginning here is now the oldest one. */
        if (s_write_address == Maintenance_SectorAddress(s_write_address))
        {
            return W25Q_Sector_Erase(&W25Q_Flash, s_write_address);
        }

        s_write_address = Maintenance_NextAddress(s_write_address);
    }

    return W25Q_ERR;
}

static uint8_t Maintenance_Append(void)
{
    maintenance_record_t record;
    uint8_t commit = MAINTENANCE_COMMIT_DONE;

    if (s_initialized == 0U)
    {
        return W25Q_ERR;
    }

    if (Maintenance_PrepareWriteSlot() != W25Q_OK)
    {
        return W25Q_ERR;
    }

    memset(&record, 0xFF, sizeof(record));
    record.magic = MAINTENANCE_MAGIC;
    record.version = MAINTENANCE_VERSION;
    record.sequence = s_next_sequence++;
    if (s_next_sequence == 0U)
    {
        s_next_sequence = 1U;
    }
    record.total_lift_count = s_status.total_lift_count;
    record.maintenance_lift_count = s_status.maintenance_lift_count;
    record.maintenance_count = s_status.maintenance_count;
    record.last_maintenance_total = s_status.last_maintenance_total;
    record.maintenance_due = s_status.maintenance_due;
    record.usage_epoch = s_status.usage_epoch;
    Maintenance_SetRecordCommand(&record);
    record.crc32 = Maintenance_RecordCrc(&record);
    record.commit = MAINTENANCE_COMMIT_EMPTY;

    if (W25Q_Write_MultiPage(&W25Q_Flash,
                             s_write_address,
                             (const uint8_t *)&record,
                             sizeof(record)) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    if (W25Q_Page_Program(&W25Q_Flash,
                          s_write_address + offsetof(maintenance_record_t, commit),
                          &commit,
                          1U) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    s_write_address = Maintenance_NextAddress(s_write_address);
    return W25Q_OK;
}

void App_Maintenance_Init(void)
{
    uint32_t sector_index;
    uint32_t newest_sequence = 0U;
    uint32_t newest_address = MAINTENANCE_INVALID_ADDRESS;
    maintenance_record_t newest_record;
    maintenance_record_t record;

    memset(&s_status, 0, sizeof(s_status));
    memset(&newest_record, 0, sizeof(newest_record));
    s_next_sequence = 1U;
    s_write_address = W25Q_MAINTENANCE_ADDR;
    s_initialized = 0U;
    s_last_command_hash = 0U;
    s_last_command_type = 0U;
    s_command_count = 0U;

    for (sector_index = 0U;
         sector_index < W25Q_MAINTENANCE_SECTOR_COUNT;
         ++sector_index)
    {
        uint32_t record_index;
        uint32_t sector_address = W25Q_MAINTENANCE_ADDR +
            (sector_index * W25Q_MAINTENANCE_SECTOR_SIZE);

        for (record_index = 0U;
             record_index < MAINTENANCE_RECORDS_PER_SECTOR;
             ++record_index)
        {
            uint32_t address = sector_address +
                (record_index * sizeof(maintenance_record_t));

            memset(&record, 0xFF, sizeof(record));
            if ((Maintenance_Read(address, &record) == W25Q_OK) &&
                (Maintenance_IsValid(&record) != 0U) &&
                ((newest_address == MAINTENANCE_INVALID_ADDRESS) ||
                 (record.sequence > newest_sequence)))
            {
                newest_sequence = record.sequence;
                newest_address = address;
                newest_record = record;
            }
            if (Maintenance_IsValid(&record) != 0U)
            {
                uint32_t command_hash = (uint32_t)record.reserved0 |
                    ((uint32_t)record.reserved1[0] << 16U) |
                    ((uint32_t)record.reserved1[1] << 24U);
                Maintenance_RememberCommand(record.reserved1[2], command_hash);
            }
        }
    }

    if (newest_address != MAINTENANCE_INVALID_ADDRESS)
    {
        s_status.total_lift_count = newest_record.total_lift_count;
        s_status.maintenance_lift_count = newest_record.maintenance_lift_count;
        s_status.maintenance_count = newest_record.maintenance_count;
        s_status.last_maintenance_total = newest_record.last_maintenance_total;
        s_status.maintenance_due = newest_record.maintenance_due;
        s_status.usage_epoch = newest_record.usage_epoch;
        Maintenance_LoadRecordCommand(&newest_record);
        s_next_sequence = newest_sequence + 1U;
        if (s_next_sequence == 0U)
        {
            s_next_sequence = 1U;
        }
        s_write_address = Maintenance_NextAddress(newest_address);
    }

    s_initialized = 1U;
}

void App_Maintenance_GetStatus(app_maintenance_status_t *out)
{
    if (out != NULL)
    {
        *out = s_status;
    }
}

uint8_t App_Maintenance_RecordRise(void)
{
    app_maintenance_status_t previous;

    if (s_initialized == 0U)
    {
        return W25Q_ERR;
    }

    previous = s_status;
    s_status.total_lift_count++;
    s_status.maintenance_lift_count++;
    if (s_status.maintenance_lift_count >= APP_MAINTENANCE_THRESHOLD)
    {
        s_status.maintenance_due = 1U;
    }

    if (Maintenance_Append() != W25Q_OK)
    {
        s_status = previous;
        return W25Q_ERR;
    }
    return W25Q_OK;
}

uint8_t App_Maintenance_Done(const char *msg_id)
{
    app_maintenance_status_t previous;
    uint32_t previous_hash;
    uint8_t previous_type;
    uint32_t command_hash = Maintenance_CommandHash(MAINTENANCE_COMMAND_DONE, msg_id);

    if ((s_initialized == 0U) || (command_hash == 0U))
    {
        return W25Q_ERR;
    }
    if (Maintenance_CommandSeen(MAINTENANCE_COMMAND_DONE, command_hash) != 0U)
    {
        return W25Q_OK;
    }

    previous = s_status;
    previous_hash = s_last_command_hash;
    previous_type = s_last_command_type;
    s_status.maintenance_count++;
    s_status.last_maintenance_total = s_status.total_lift_count;
    s_status.maintenance_lift_count = 0U;
    s_status.maintenance_due = 0U;
    s_last_command_hash = command_hash;
    s_last_command_type = MAINTENANCE_COMMAND_DONE;

    if (Maintenance_Append() != W25Q_OK)
    {
        s_status = previous;
        s_last_command_hash = previous_hash;
        s_last_command_type = previous_type;
        return W25Q_ERR;
    }
    Maintenance_RememberCommand(MAINTENANCE_COMMAND_DONE, command_hash);

    return W25Q_OK;
}

uint8_t App_Maintenance_ResetUsage(const char *msg_id)
{
    app_maintenance_status_t previous;
    uint32_t previous_hash;
    uint8_t previous_type;
    uint32_t command_hash = Maintenance_CommandHash(MAINTENANCE_COMMAND_RESET, msg_id);

    if ((s_initialized == 0U) || (command_hash == 0U))
    {
        return W25Q_ERR;
    }
    if (Maintenance_CommandSeen(MAINTENANCE_COMMAND_RESET, command_hash) != 0U)
    {
        return W25Q_OK;
    }

    previous = s_status;
    previous_hash = s_last_command_hash;
    previous_type = s_last_command_type;
    s_status.total_lift_count = 0U;
    s_status.maintenance_lift_count = 0U;
    s_status.maintenance_count = 0U;
    s_status.last_maintenance_total = 0U;
    s_status.maintenance_due = 0U;
    s_status.usage_epoch++;
    s_last_command_hash = command_hash;
    s_last_command_type = MAINTENANCE_COMMAND_RESET;

    if (Maintenance_Append() != W25Q_OK)
    {
        s_status = previous;
        s_last_command_hash = previous_hash;
        s_last_command_type = previous_type;
        return W25Q_ERR;
    }
    Maintenance_RememberCommand(MAINTENANCE_COMMAND_RESET, command_hash);

    return W25Q_OK;
}

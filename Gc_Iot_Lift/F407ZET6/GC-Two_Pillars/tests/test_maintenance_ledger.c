#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define THRESHOLD 5000U
#define MAGIC 0x4D41494EU
#define VERSION 1U
#define COMMIT 0x434D4954U

typedef struct {
    uint32_t total;
    uint32_t cycle;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint32_t epoch;
    uint8_t due;
} maintenance_t;

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint32_t total;
    uint32_t cycle;
    uint32_t maintenance_count;
    uint32_t last_maintenance_total;
    uint32_t epoch;
    uint8_t due;
    uint8_t reserved0;
    uint16_t version;
    uint32_t crc32;
    uint32_t commit;
    uint8_t reserved1[24];
} record_t;

typedef char record_must_be_64[(sizeof(record_t) == 64U) ? 1 : -1];

static uint32_t crc32(const uint8_t *data, uint16_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return ~crc;
}

static void add_units(maintenance_t *state, uint32_t units)
{
    state->total += units;
    state->cycle += units;
    if (state->cycle >= THRESHOLD) state->due = 1U;
}

static uint32_t qualified_units(uint32_t continuous_rise_ms)
{
    return continuous_rise_ms / 3000U;
}

static void maintenance_done(maintenance_t *state)
{
    state->last_maintenance_total = state->total;
    state->cycle = 0U;
    state->maintenance_count++;
    state->due = 0U;
}

static void reset_usage(maintenance_t *state)
{
    uint32_t next_epoch = state->epoch + 1U;
    memset(state, 0, sizeof(*state));
    state->epoch = next_epoch;
}

static void make_record(record_t *record, const maintenance_t *state, uint32_t sequence)
{
    memset(record, 0, sizeof(*record));
    record->magic = MAGIC;
    record->sequence = sequence;
    record->total = state->total;
    record->cycle = state->cycle;
    record->maintenance_count = state->maintenance_count;
    record->last_maintenance_total = state->last_maintenance_total;
    record->epoch = state->epoch;
    record->due = state->due;
    record->version = VERSION;
    record->crc32 = crc32((const uint8_t *)record, 32U);
    record->commit = COMMIT;
}

static uint8_t record_is_valid(const record_t *record)
{
    return (record->magic == MAGIC && record->version == VERSION &&
            record->commit == COMMIT && record->due <= 1U &&
            record->crc32 == crc32((const uint8_t *)record, 32U)) ? 1U : 0U;
}

static uint8_t duplicate_msg_id(const char *const *seen, uint8_t count, const char *msg_id)
{
    uint8_t i;
    for (i = 0U; i < count; i++) {
        if (strcmp(seen[i], msg_id) == 0) return 1U;
    }
    return 0U;
}

int main(void)
{
    maintenance_t state = {0};
    record_t records[3];
    const char *seen[] = {"maint-1"};
    uint32_t pending_epoch;

    /* Main and sub pillars each report their local continuous rise time: one
     * qualified unit per 3 seconds, never one unit per motor. */
    assert(qualified_units(2999U) == 0U);
    assert(qualified_units(3000U) == 1U); /* main pillar */
    assert(qualified_units(6000U) == 2U); /* sub pillar's independent device */
    add_units(&state, 4999U);
    assert(state.total == 4999U && state.cycle == 4999U && state.due == 0U);
    add_units(&state, 1U);
    assert(state.total == 5000U && state.cycle == 5000U && state.due == 1U);

    make_record(&records[0], &state, 10U);
    make_record(&records[1], &state, 11U);
    records[1].total++;
    make_record(&records[2], &state, 12U);
    records[2].commit = 0xFFFFFFFFU;
    assert(record_is_valid(&records[0]) == 1U);
    assert(record_is_valid(&records[1]) == 0U); /* CRC damage */
    assert(record_is_valid(&records[2]) == 0U); /* power loss before commit */

    maintenance_done(&state);
    assert(state.total == 5000U && state.cycle == 0U && state.maintenance_count == 1U);
    assert(state.last_maintenance_total == 5000U && state.due == 0U);
    assert(duplicate_msg_id(seen, 1U, "maint-1") == 1U);
    assert(duplicate_msg_id(seen, 1U, "maint-2") == 0U);

    pending_epoch = state.epoch;
    reset_usage(&state);
    assert(state.total == 0U && state.cycle == 0U && state.maintenance_count == 0U);
    assert(state.due == 0U && state.epoch == pending_epoch + 1U);
    assert(pending_epoch != state.epoch); /* reject reset-before-send upload */

    puts("maintenance ledger tests passed");
    return 0;
}

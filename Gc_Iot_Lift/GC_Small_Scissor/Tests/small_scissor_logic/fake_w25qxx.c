#include "fake_w25qxx.h"
#include "app_w25qxx.h"
#include <string.h>

static fake_stats_t s_stats;
#define FAKE_MAINTENANCE_HISTORY_SIZE 8U
static w25q_maintenance_ledger_t s_maintenance_history[FAKE_MAINTENANCE_HISTORY_SIZE];
static uint32_t s_maintenance_history_count;

static uint32_t fake_command_hash(uint8_t command, const char *msg_id)
{
    uint32_t hash = 2166136261U;

    if ((msg_id == NULL) || (msg_id[0] == '\0')) {
        return 0U;
    }
    while (*msg_id != '\0') {
        hash ^= (uint8_t)*msg_id++;
        hash *= 16777619U;
    }
    return hash ^ ((uint32_t)command << 24);
}

static void fake_load_latest(w25q_maintenance_ledger_t *ledger)
{
    if (s_maintenance_history_count == 0U) {
        memset(ledger, 0, sizeof(*ledger));
    } else {
        *ledger = s_maintenance_history[(s_maintenance_history_count - 1U) % FAKE_MAINTENANCE_HISTORY_SIZE];
    }
}

static void fake_append(const w25q_maintenance_ledger_t *ledger)
{
    w25q_maintenance_ledger_t saved = *ledger;
    saved.sequence = s_maintenance_history_count + 1U;
    s_maintenance_history[s_maintenance_history_count % FAKE_MAINTENANCE_HISTORY_SIZE] = saved;
    s_maintenance_history_count++;
}

static uint8_t fake_seen_command(const w25q_maintenance_ledger_t *ledger, uint32_t hash)
{
    uint32_t index;

    for (index = 0U; index < W25Q_MAINTENANCE_COMMAND_CACHE_SIZE; index++) {
        if ((hash != 0U) && (ledger->command_hashes[index] == hash)) {
            return 1U;
        }
    }
    return 0U;
}

static void fake_remember_command(w25q_maintenance_ledger_t *ledger, uint32_t hash)
{
    uint32_t index;

    for (index = W25Q_MAINTENANCE_COMMAND_CACHE_SIZE - 1U; index > 0U; index--) {
        ledger->command_hashes[index] = ledger->command_hashes[index - 1U];
    }
    ledger->command_hashes[0] = hash;
}

void fake_w25qxx_reset(void)
{
    s_stats = (fake_stats_t){0};
    memset(s_maintenance_history, 0, sizeof(s_maintenance_history));
    s_maintenance_history_count = 0U;
}

const fake_stats_t *fake_w25qxx_stats(void)
{
    return &s_stats;
}

void App_W25Qxx_Stats_Inc_Up(lift_role_t role)
{
    (void)role;
    s_stats.up++;
}

void App_W25Qxx_Stats_Inc_Down(lift_role_t role)
{
    (void)role;
    s_stats.down++;
}

void App_W25Qxx_Stats_Inc_Lock(void)
{
    s_stats.lock++;
}

void App_W25Qxx_Stats_Inc_Refill(void)
{
    s_stats.refill++;
}

void App_W25Qxx_Stats_Inc_Estop(void)
{
    s_stats.estop++;
}

void App_W25Qxx_Stats_Inc_PhotoAlarm(void)
{
    s_stats.photo_alarm++;
}

void App_W25Qxx_Stats_Add_RunMs(uint32_t ms)
{
    (void)ms;
}

void fake_w25qxx_corrupt_latest(void)
{
    if (s_maintenance_history_count != 0U) {
        s_maintenance_history[(s_maintenance_history_count - 1U) % FAKE_MAINTENANCE_HISTORY_SIZE].sequence = 0U;
        s_maintenance_history_count--;
    }
}

uint8_t App_W25Qxx_Maintenance_Load(w25q_maintenance_ledger_t *ledger)
{
    if (ledger == NULL) {
        return W25Q_ERR;
    }
    fake_load_latest(ledger);
    return W25Q_OK;
}

uint8_t App_W25Qxx_Maintenance_Increment(w25q_maintenance_ledger_t *ledger)
{
    w25q_maintenance_ledger_t current;

    if (ledger == NULL) {
        return W25Q_ERR;
    }
    fake_load_latest(&current);
    current.total_lift_count++;
    current.maintenance_lift_count++;
    current.maintenance_due = (current.maintenance_lift_count >= W25Q_MAINTENANCE_THRESHOLD) ? 1U : 0U;
    fake_append(&current);
    fake_load_latest(ledger);
    return W25Q_OK;
}

uint8_t App_W25Qxx_Maintenance_Done(const char *msg_id,
                                    w25q_maintenance_ledger_t *ledger)
{
    w25q_maintenance_ledger_t current;
    uint32_t hash = fake_command_hash(1U, msg_id);

    if ((ledger == NULL) || (hash == 0U)) {
        return W25Q_ERR;
    }
    fake_load_latest(&current);
    if (fake_seen_command(&current, hash) == 0U) {
        current.last_maintenance_total = current.total_lift_count;
        current.maintenance_lift_count = 0U;
        current.maintenance_count++;
        current.maintenance_due = 0U;
        fake_remember_command(&current, hash);
        fake_append(&current);
    }
    fake_load_latest(ledger);
    return W25Q_OK;
}

uint8_t App_W25Qxx_Maintenance_ResetUsage(const char *msg_id,
                                          w25q_maintenance_ledger_t *ledger)
{
    w25q_maintenance_ledger_t current;
    uint32_t hash = fake_command_hash(2U, msg_id);
    uint32_t epoch;

    if ((ledger == NULL) || (hash == 0U)) {
        return W25Q_ERR;
    }
    fake_load_latest(&current);
    if (fake_seen_command(&current, hash) == 0U) {
        epoch = current.usage_epoch + 1U;
        memset(&current, 0, sizeof(current));
        current.usage_epoch = epoch;
        fake_remember_command(&current, hash);
        fake_append(&current);
    }
    fake_load_latest(ledger);
    return W25Q_OK;
}

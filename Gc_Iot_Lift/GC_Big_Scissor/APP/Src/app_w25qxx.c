#include "app_w25qxx.h"
#include "app_maintenance.h"
#include "app_rise_queue.h"
#include "app_spi.h"
#include "app_product.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#if W25Q_DEBUG == 1
#include "elog.h"
#endif

w25q_storage_t g_w25q_storage = {0};

w25q_config_t g_config = {
    .header                   = W25Q_CONFIG_HEADER,
    .version                  = W25Q_CONFIG_VERSION,
    .product_type             = PRODUCT_TYPE_LARGE_SCISSOR,
    .reserved1                = 0,
    .motor_to_valve_delay_ms  = 200,
    .motor_hold_ms            = 2500,
    .sub_motor_hold_ms        = 1500,
    .module_enable_mask       = 0,
    .reserved2                = 0,
    .photoelectric_debounce_ms= 50,
    .estop_debounce_ms        = 20,
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
    .bus = &SPI_Bus
};

void App_W25Qxx_System_Init(void)
{
    uint8_t result;

    App_SPI_System_Init();
    result = W25Q_Init_Device(&W25Q_Flash);
    if (result == W25Q_OK)
    {
        g_stats.magic = W25Q_STATS_MAGIC;
        g_stats.version = W25Q_STATS_VERSION;
        App_Maintenance_Init();
        App_RiseQueue_Init();
    }
}

uint32_t App_W25Qxx_Get_JEDEC_ID(void)
{
    return W25Q_Read_JEDEC_ID(&W25Q_Flash);
}

void App_W25Qxx_Storage_Load(void)
{
}

uint8_t App_W25Qxx_Storage_Save(void)
{
    return W25Q_OK;
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
}

uint8_t App_W25Qxx_Stats_Save(void)
{
    return W25Q_OK;
}

void App_W25Qxx_Stats_Inc_Up(lift_role_t role)
{
    app_maintenance_status_t maintenance;

    /* A qualified rise becomes official only after its ledger append commits. */
    if (App_Maintenance_RecordRise() != W25Q_OK)
    {
        return;
    }

    App_Maintenance_GetStatus(&maintenance);
    taskENTER_CRITICAL();
    g_stats.up_count++;
    if (role == LIFT_ROLE_MAIN) g_stats.up_count_main++;
    else                        g_stats.up_count_sub++;

    App_RiseQueue_Enqueue(role,
                          g_stats.up_count,
                          g_stats.up_count_main,
                          g_stats.up_count_sub,
                          maintenance.usage_epoch);
    taskEXIT_CRITICAL();
}

uint8_t App_W25Qxx_Stats_ResetUsage(uint32_t usage_epoch)
{
    if (App_RiseQueue_ResetUsage(usage_epoch) != W25Q_OK)
    {
        return W25Q_ERR;
    }

    taskENTER_CRITICAL();
    g_stats.up_count = 0U;
    g_stats.up_count_main = 0U;
    g_stats.up_count_sub = 0U;
    taskEXIT_CRITICAL();

    return W25Q_OK;
}

void App_W25Qxx_Stats_Inc_Down(lift_role_t role)
{
    g_stats.down_count++;
    if (role == LIFT_ROLE_MAIN) g_stats.down_count_main++;
    else                        g_stats.down_count_sub++;
}

void App_W25Qxx_Stats_Inc_Lock(void)       { g_stats.lock_count++; }
void App_W25Qxx_Stats_Inc_Refill(void)     { g_stats.refill_count++; }
void App_W25Qxx_Stats_Inc_Estop(void)      { g_stats.estop_count++; }
void App_W25Qxx_Stats_Inc_PhotoAlarm(void) { g_stats.photo_alarm_count++; }
void App_W25Qxx_Stats_Add_RunMs(uint32_t ms) { g_stats.total_run_ms += ms; }

uint32_t App_W25Qxx_Stats_GetRiseRemainder(lift_role_t role)
{
    return App_RiseQueue_GetRemainder(role);
}

void App_W25Qxx_Stats_SetRiseRemainder(lift_role_t role, uint32_t remainder_ms)
{
    App_RiseQueue_SetRemainder(role, remainder_ms);
}

uint8_t App_W25Qxx_OpLog_Append(const w25q_op_log_entry_t *entry)
{
    (void)entry;
    return W25Q_OK;
}

uint8_t App_W25Qxx_OpLog_Read(uint16_t index, w25q_op_log_entry_t *out)
{
    (void)index;
    if (out != NULL) memset(out, 0, sizeof(*out));
    return W25Q_ERR;
}

uint16_t App_W25Qxx_OpLog_Count(void)
{
    return 0U;
}

void App_W25Qxx_OpLog_Clear(void)
{
}

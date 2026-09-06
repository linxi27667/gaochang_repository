#include "lift_iot.h"

#include "app_io_map.h"
#include "app_op_log.h"
#include "app_product.h"
#include "app_w25qxx.h"
#include "elog.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

static lift_iot_status_t g_lift_iot_status;
static lift_state_t g_lift_iot_last_state = STATE_IDLE;
static uint32_t g_lift_iot_motion_start_tick;
static volatile uint8_t g_iot_event_pending;

#define LIFT_IOT_RISE_COUNT_PERIOD_MS 3000U
#define LIFT_IOT_JSON_SIZE              1536U

static w25q_maintenance_ledger_t g_lift_iot_maintenance;
static uint32_t g_lift_iot_rise_elapsed_ms;
static uint32_t g_lift_iot_pending_lift_counts;
static uint32_t g_lift_iot_rise_last_tick;
static uint8_t g_lift_iot_rise_last_qualified;
static uint8_t g_lift_iot_rise_sample_initialized;

static uint32_t LiftIot_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

static void LiftIot_ExitCritical(uint32_t primask)
{
    __set_PRIMASK(primask);
}

static uint8_t LiftIot_IsQualifiedRise(const lift_ctx_t *ctx)
{
    return ((ctx != NULL) &&
            (ctx->current_state == STATE_RISING) &&
            (ctx->output_actual.motor_on != 0U) &&
            (ctx->output_actual.down_valve_on == 0U)) ? 1U : 0U;
}

static void LiftIot_UpdateRiseCounter(uint32_t now, const lift_ctx_t *ctx)
{
    uint8_t qualified = LiftIot_IsQualifiedRise(ctx);
    uint32_t elapsed;

    if (g_lift_iot_rise_sample_initialized == 0U)
    {
        g_lift_iot_rise_last_tick = now;
        g_lift_iot_rise_last_qualified = qualified;
        g_lift_iot_rise_sample_initialized = 1U;
        return;
    }

    elapsed = now - g_lift_iot_rise_last_tick;
    if (g_lift_iot_rise_last_qualified != 0U)
    {
        if (elapsed != 0U)
        {
            g_lift_iot_rise_elapsed_ms += elapsed;
            while (g_lift_iot_rise_elapsed_ms >= LIFT_IOT_RISE_COUNT_PERIOD_MS)
            {
                g_lift_iot_rise_elapsed_ms -= LIFT_IOT_RISE_COUNT_PERIOD_MS;
                g_lift_iot_pending_lift_counts++;
            }
        }
    }

    if (qualified == 0U)
    {
        /* A release, limit, or safety transition ends this continuous rise interval. */
        g_lift_iot_rise_elapsed_ms = 0U;
    }

    while (g_lift_iot_pending_lift_counts != 0U)
    {
        w25q_maintenance_ledger_t saved;

        if (App_W25Qxx_Maintenance_Increment(&saved) != W25Q_OK)
        {
            break;
        }
        g_lift_iot_maintenance = saved;
        g_lift_iot_status.maintenance_due = (uint8_t)saved.maintenance_due;
        g_lift_iot_pending_lift_counts--;
        g_iot_event_pending = 1U;
    }

    g_lift_iot_rise_last_tick = now;
    g_lift_iot_rise_last_qualified = qualified;
}

static uint8_t LiftIot_IsMotionState(lift_state_t state)
{
    return ((state == STATE_RISING) ||
            (state == STATE_DOWN_PREPARE) ||
            (state == STATE_DOWN_HOLD_MOTOR) ||
            (state == STATE_DROPPING) ||
            (state == STATE_LOCKING) ||
            (state == STATE_REFILL)) ? 1U : 0U;
}

static const char *LiftIot_AlarmName(const lift_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return "none";
    }

    if (ctx->alarm.estop_active != 0U)
    {
        return "estop";
    }
    if (ctx->alarm.photo_alarm_latched != 0U)
    {
        return "photo_alarm";
    }
    if (ctx->alarm.timeout_latched != 0U)
    {
        return "stall";
    }
    if ((ctx->alarm.invalid_input_latched != 0U) ||
        (ctx->alarm.output_mismatch_latched != 0U) ||
        (ctx->alarm.fault_latched != 0U))
    {
        return "fault";
    }

    return "none";
}

static uint8_t LiftIot_AlarmCode(const lift_ctx_t *ctx)
{
    const char *alarm = LiftIot_AlarmName(ctx);

    if (strcmp(alarm, "none") == 0)
    {
        return 0U;
    }
    if (strcmp(alarm, "photo_alarm") == 0)
    {
        return 1U;
    }
    if (strcmp(alarm, "estop") == 0)
    {
        return 2U;
    }
    if (strcmp(alarm, "stall") == 0)
    {
        return 3U;
    }

    return 4U;
}

static void LiftIot_UpdateMotionStat(uint32_t now, lift_state_t cur)
{
    uint8_t cur_motion = LiftIot_IsMotionState(cur);
    uint8_t last_motion = LiftIot_IsMotionState(g_lift_iot_last_state);

    if ((last_motion == 0U) && (cur_motion != 0U))
    {
        g_lift_iot_motion_start_tick = now;
        g_lift_iot_status.current_run_ms = 0U;
        g_lift_iot_status.run_count++;
    }
    else if ((last_motion != 0U) && (cur_motion == 0U))
    {
        uint32_t duration = now - g_lift_iot_motion_start_tick;
        g_lift_iot_status.total_run_ms += duration;
        g_lift_iot_status.current_run_ms = 0U;
        App_W25Qxx_Stats_Add_RunMs(duration);
    }
    else if (cur_motion != 0U)
    {
        g_lift_iot_status.current_run_ms = now - g_lift_iot_motion_start_tick;
    }

    if (cur != g_lift_iot_last_state)
    {
        g_iot_event_pending = 1U;
    }

    g_lift_iot_last_state = cur;
}

uint8_t LiftIot_PeekEventFlag(void)
{
    return g_iot_event_pending;
}

uint8_t LiftIot_ConsumeEventFlag(void)
{
    uint8_t old = g_iot_event_pending;
    g_iot_event_pending = 0U;
    return old;
}

uint8_t LiftIot_IsInMotion(void)
{
    const lift_ctx_t *ctx = App_LiftCore_GetContext();
    return (ctx != NULL) ? LiftIot_IsMotionState(ctx->current_state) : 0U;
}

void LiftIot_Init(void)
{
    memset(&g_lift_iot_status, 0, sizeof(g_lift_iot_status));
    g_lift_iot_last_state = STATE_IDLE;
    g_iot_event_pending = 0U;

    memset(&g_lift_iot_maintenance, 0, sizeof(g_lift_iot_maintenance));
    g_lift_iot_rise_elapsed_ms = 0U;
    g_lift_iot_pending_lift_counts = 0U;
    g_lift_iot_rise_last_tick = 0U;
    g_lift_iot_rise_last_qualified = 0U;
    g_lift_iot_rise_sample_initialized = 0U;

    if (App_W25Qxx_Maintenance_Load(&g_lift_iot_maintenance) == W25Q_OK)
    {
        g_lift_iot_status.maintenance_due = (uint8_t)g_lift_iot_maintenance.maintenance_due;
    }
}

void LiftIot_Poll(void)
{
    const lift_ctx_t *ctx = App_LiftCore_GetContext();
    uint32_t now;

    if (ctx == NULL)
    {
        return;
    }

    now = HAL_GetTick();
    LiftIot_UpdateMotionStat(now, ctx->current_state);
    LiftIot_UpdateRiseCounter(now, ctx);
}

uint8_t LiftIot_GetRiseCounterSaveSnapshot(uint32_t *count,
                                           uint32_t *remainder_ms,
                                           uint32_t *generation)
{
    (void)count;
    (void)remainder_ms;
    (void)generation;
    return 0U;
}

void LiftIot_ConfirmRiseCounterSaved(uint32_t generation, uint32_t flash_sequence)
{
    (void)generation;
    (void)flash_sequence;
}

uint8_t LiftIot_GetRiseCounterReportSnapshot(uint32_t *count,
                                             uint32_t *remainder_ms,
                                             uint32_t *flash_sequence)
{
    (void)count;
    (void)remainder_ms;
    (void)flash_sequence;
    return 0U;
}

void LiftIot_MarkRiseCounterReported(uint32_t flash_sequence)
{
    (void)flash_sequence;
}

uint8_t LiftIot_IsLocked(void)
{
    return g_lift_iot_status.locked;
}

lift_iot_result_t LiftIot_SetLocked(uint8_t locked, const char *source)
{
    g_lift_iot_status.locked = (locked != 0U) ? 1U : 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;

    App_LiftCore_SetRemoteLock(g_lift_iot_status.locked);
    if (g_lift_iot_status.locked != 0U)
    {
        App_IO_All_Off();
    }

    elog_w("IOT", "[IOT] remote_%s account=%s",
           g_lift_iot_status.locked ? "lock" : "unlock",
           (source != NULL) ? source : "remote");

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_EnterAdmin(const char *password, const char *account)
{
    if (password == NULL)
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    if (strcmp(password, LIFT_IOT_ADMIN_PASSWORD) != 0)
    {
        elog_w("IOT", "[IOT] admin denied account=%s",
               (account != NULL) ? account : "unknown");
        return LIFT_IOT_DENIED;
    }

    g_lift_iot_status.admin_mode = 1U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    return LIFT_IOT_OK;
}

void LiftIot_ExitAdmin(void)
{
    g_lift_iot_status.admin_mode = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
}

lift_iot_result_t LiftIot_ClearFault(const char *account)
{
    if (g_lift_iot_status.admin_mode == 0U)
    {
        return LIFT_IOT_DENIED;
    }

    App_LiftCore_RequestClearFault((account != NULL) ? account : "remote");
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_AdminJog(uint8_t column_index,
                                   uint8_t direction_up,
                                   uint32_t duration_ms,
                                   const char *account)
{
    (void)column_index;
    (void)direction_up;
    (void)duration_ms;
    (void)account;
    return LIFT_IOT_DENIED;
}

void LiftIot_GetMaintenance(lift_iot_maintenance_t *maintenance)
{
    uint32_t primask;

    if (maintenance == NULL)
    {
        return;
    }
    primask = LiftIot_EnterCritical();
    maintenance->total_lift_count = g_lift_iot_maintenance.total_lift_count;
    maintenance->maintenance_lift_count = g_lift_iot_maintenance.maintenance_lift_count;
    maintenance->maintenance_count = g_lift_iot_maintenance.maintenance_count;
    maintenance->last_maintenance_total = g_lift_iot_maintenance.last_maintenance_total;
    maintenance->maintenance_due = g_lift_iot_maintenance.maintenance_due;
    maintenance->usage_epoch = g_lift_iot_maintenance.usage_epoch;
    LiftIot_ExitCritical(primask);
}

lift_iot_result_t LiftIot_MaintenanceDone(const char *account, const char *msg_id)
{
    w25q_maintenance_ledger_t saved;

    (void)account;
    if ((msg_id == NULL) || (msg_id[0] == '\0') ||
        (App_W25Qxx_Maintenance_Done(msg_id, &saved) != W25Q_OK))
    {
        return LIFT_IOT_DENIED;
    }
    g_lift_iot_maintenance = saved;
    g_lift_iot_status.maintenance_due = (uint8_t)saved.maintenance_due;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_ResetUsage(const char *account, const char *msg_id)
{
    w25q_maintenance_ledger_t saved;

    (void)account;
    if ((msg_id == NULL) || (msg_id[0] == '\0') ||
        (App_W25Qxx_Maintenance_ResetUsage(msg_id, &saved) != W25Q_OK))
    {
        return LIFT_IOT_DENIED;
    }
    g_lift_iot_maintenance = saved;
    g_lift_iot_status.maintenance_due = 0U;
    g_lift_iot_pending_lift_counts = 0U;
    g_lift_iot_rise_elapsed_ms = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    return LIFT_IOT_OK;
}

const lift_iot_status_t *LiftIot_GetStatus(void)
{
    return &g_lift_iot_status;
}

const char *LiftIot_StateName(void)
{
    const lift_ctx_t *ctx = App_LiftCore_GetContext();

    if (g_lift_iot_status.locked != 0U)
    {
        return "locked";
    }
    if (ctx == NULL)
    {
        return "unknown";
    }

    switch (ctx->current_state)
    {
        case STATE_RISING:
            return "rising";
        case STATE_DOWN_PREPARE:
        case STATE_DOWN_HOLD_MOTOR:
        case STATE_DROPPING:
            return "dropping";
        case STATE_REFILL:
            return "refilling";
        case STATE_LOCKING:
            return "locked";
        case STATE_ESTOP:
            return "estop";
        case STATE_PHOTO_ALARM:
            return "photo_alarm";
        case STATE_FAULT:
            return "fault";
        case STATE_INIT:
            return "init";
        case STATE_SAFE_STOP:
            return "stop";
        case STATE_IDLE:
        default:
            return "idle";
    }
}

lift_iot_result_t LiftIot_BuildTelemetryJson(char *buf,
                                             uint16_t size,
                                             const char *type,
                                             uint32_t seq,
                                             const char *dtu_state,
                                             int16_t csq)
{
    const lift_ctx_t *ctx = App_LiftCore_GetContext();
    uint32_t now = HAL_GetTick();
    uint32_t avg_ms;
    lift_iot_maintenance_t maintenance;
    int len;

    if ((buf == NULL) || (type == NULL) || (dtu_state == NULL) || (ctx == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    avg_ms = (g_lift_iot_status.run_count == 0U)
             ? 0U
             : (g_lift_iot_status.total_run_ms / g_lift_iot_status.run_count);

    LiftIot_GetMaintenance(&maintenance);

    len = snprintf(buf, size,
                   "{\"type\":\"%s\",\"device\":\"%s\",\"uid\":\"%s\","
                   "\"product_type\":\"small_scissor\",\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,"
                   "\"state\":\"%s\",\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,\"buzzer_on\":0,"
                   "\"io_input\":{\"btn_up\":%u,\"btn_down\":%u,\"btn_lock\":%u,\"estop\":%u,"
                   "\"upper_limit\":%u,\"btn_refill\":%u,\"photoelectric\":%u,\"lower_limit\":%u},"
                   "\"io_output\":{\"motor\":%u,\"drop_valve\":%u,\"valve_air\":%u},"
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%u,\"upper\":%u,\"estop\":%u,\"photoelectric\":%u,\"lower\":%u},"
                     "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,\"maintenance_threshold\":%u,\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,\"maintenance_due\":%lu,\"usage_epoch\":%lu},"
                     "\"stats\":{\"up\":%lu,\"rise_3s_count\":%lu,\"rise_remainder_ms\":%u,\"down\":%lu,\"lock\":%lu,\"refill\":%lu,\"estop\":%lu,\"photo_alarm\":%lu,\"total_run_ms\":%lu},"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   type,
                   LIFT_IOT_DEVICE_ID,
                   App_Product_GetUIDString(),
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   LiftIot_StateName(),
                   (unsigned int)g_lift_iot_status.locked,
                   (unsigned int)g_lift_iot_status.maintenance_due,
                   (unsigned int)g_lift_iot_status.admin_mode,
                   (unsigned int)ctx->input.pressed[LIFT_IN_UP],
                   (unsigned int)ctx->input.pressed[LIFT_IN_DOWN],
                   (unsigned int)ctx->input.pressed[LIFT_IN_LOCK],
                   (unsigned int)ctx->input.pressed[LIFT_IN_ESTOP],
                   (unsigned int)ctx->input.pressed[LIFT_IN_UPPER_LIMIT],
                   (unsigned int)ctx->input.pressed[LIFT_IN_REFILL],
                   (unsigned int)ctx->input.pressed[LIFT_IN_PHOTO],
                   (unsigned int)ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
                   (unsigned int)ctx->output_actual.motor_on,
                   (unsigned int)ctx->output_actual.down_valve_on,
                   (unsigned int)ctx->output_actual.air_valve_on,
                   LiftIot_AlarmName(ctx),
                   (unsigned int)LiftIot_AlarmCode(ctx),
                   (unsigned int)ctx->input.pressed[LIFT_IN_UPPER_LIMIT],
                    (unsigned int)ctx->input.pressed[LIFT_IN_ESTOP],
                    (unsigned int)ctx->input.pressed[LIFT_IN_PHOTO],
                    (unsigned int)ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
                    (unsigned long)maintenance.total_lift_count,
                    (unsigned long)maintenance.maintenance_lift_count,
                    (unsigned int)W25Q_MAINTENANCE_THRESHOLD,
                    (unsigned long)maintenance.maintenance_count,
                    (unsigned long)maintenance.last_maintenance_total,
                    (unsigned long)maintenance.maintenance_due,
                    (unsigned long)maintenance.usage_epoch,
                    (unsigned long)ctx->up_count,
                     (unsigned long)maintenance.total_lift_count,
                     0U,
                    (unsigned long)ctx->down_count,
                   (unsigned long)ctx->lock_count,
                   (unsigned long)ctx->refill_count,
                   (unsigned long)ctx->estop_count,
                   (unsigned long)ctx->photo_alarm_count,
                   (unsigned long)g_lift_iot_status.total_run_ms,
                   (unsigned long)g_lift_iot_status.total_run_ms,
                   (unsigned long)g_lift_iot_status.current_run_ms,
                   (unsigned long)g_lift_iot_status.run_count,
                   (unsigned long)avg_ms,
                   dtu_state,
                   (int)csq);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_BuildRiseCounterJson(char *buf,
                                                uint16_t size,
                                                uint32_t tx_seq,
                                                uint32_t total_count,
                                                uint32_t remainder_ms,
                                                uint32_t flash_sequence,
                                                const char *dtu_state,
                                                int16_t csq)
{
    lift_iot_maintenance_t maintenance;
    int len;

    if ((buf == NULL) || (dtu_state == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    LiftIot_GetMaintenance(&maintenance);
    len = snprintf(buf, size,
                   "{\"type\":\"rise_counter\",\"device\":\"%s\",\"serial\":\"%s\",\"sn\":\"%s\","
                   "\"uid\":\"%s\",\"chip_uid\":\"%s\",\"product_type\":\"small_scissor\","
                    "\"seq\":%lu,\"tick\":%lu,\"event_id\":%lu,\"flash_sequence\":%lu,\"maintenance_due\":%lu,"
                    "\"rise_3s_count\":%lu,\"rise_remainder_ms\":%lu,"
                    "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,\"maintenance_threshold\":%u,\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,\"maintenance_due\":%lu,\"usage_epoch\":%lu},\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   App_Product_GetUIDString(),
                   App_Product_GetUIDString(),
                   (unsigned long)tx_seq,
                    (unsigned long)HAL_GetTick(),
                    (unsigned long)flash_sequence,
                    (unsigned long)flash_sequence,
                    (unsigned long)maintenance.maintenance_due,
                    (unsigned long)total_count,
                    (unsigned long)remainder_ms,
                    (unsigned long)maintenance.total_lift_count,
                    (unsigned long)maintenance.maintenance_lift_count,
                    (unsigned int)W25Q_MAINTENANCE_THRESHOLD,
                    (unsigned long)maintenance.maintenance_count,
                    (unsigned long)maintenance.last_maintenance_total,
                    (unsigned long)maintenance.maintenance_due,
                    (unsigned long)maintenance.usage_epoch,
                    dtu_state,
                   (int)csq);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_BuildStatusJson(char *buf,
                                          uint16_t size,
                                          const char *event,
                                          uint32_t seq,
                                          const char *dtu_state,
                                          int16_t csq)
{
    char json[LIFT_IOT_JSON_SIZE];
    int len;

    if ((buf == NULL) || (event == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    if (LiftIot_BuildTelemetryJson(json, sizeof(json), "status", seq, dtu_state, csq) != LIFT_IOT_OK)
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    len = snprintf(buf, size, "%.*s,\"event\":\"%s\"}",
                   (int)(strlen(json) - 1U),
                   json,
                   event);
    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_BuildCommandStatusJson(char *buf,
                                                 uint16_t size,
                                                 const char *event,
                                                 const char *cmd,
                                                 const char *msg_id,
                                                 const char *result,
                                                 uint32_t seq,
                                                 const char *dtu_state,
                                                 int16_t csq)
{
    char json[LIFT_IOT_JSON_SIZE];
    int len;

    if ((buf == NULL) || (event == NULL) || (cmd == NULL) ||
        (msg_id == NULL) || (result == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    if (LiftIot_BuildStatusJson(json, sizeof(json), event, seq, dtu_state, csq) != LIFT_IOT_OK)
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    len = snprintf(buf, size, "%.*s,\"cmd\":\"%s\",\"msg_id\":\"%s\",\"result\":\"%s\"}",
                   (int)(strlen(json) - 1U),
                   json,
                   cmd,
                   msg_id,
                   result);
    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_BuildHeightJson(char *buf, uint16_t size, uint32_t seq)
{
    const lift_ctx_t *ctx = App_LiftCore_GetContext();
    lift_iot_maintenance_t maintenance;
    int len;

    if ((buf == NULL) || (ctx == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    LiftIot_GetMaintenance(&maintenance);
    len = snprintf(buf, size,
                   "{\"type\":\"height\",\"device\":\"%s\",\"serial\":\"%s\",\"sn\":\"%s\","
                   "\"uid\":\"%s\",\"chip_uid\":\"%s\",\"product_type\":\"small_scissor\","
                    "\"seq\":%lu,\"tick\":%lu,\"state\":\"%s\",\"alarm\":\"%s\",\"maintenance_due\":%lu,"
                    "\"io_input\":{\"estop\":%u,\"upper_limit\":%u,\"photoelectric\":%u,\"lower_limit\":%u},"
                    "\"safety\":{\"alarm\":\"%s\",\"upper\":%u,\"estop\":%u,\"photoelectric\":%u,\"lower\":%u},"
                    "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,\"maintenance_threshold\":%u,\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,\"maintenance_due\":%lu,\"usage_epoch\":%lu},\"note\":\"small_scissor_no_encoder\"}",
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   App_Product_GetUIDString(),
                   App_Product_GetUIDString(),
                   (unsigned long)seq,
                    (unsigned long)HAL_GetTick(),
                    LiftIot_StateName(),
                    LiftIot_AlarmName(ctx),
                    (unsigned long)maintenance.maintenance_due,
                   (unsigned int)ctx->input.pressed[LIFT_IN_ESTOP],
                   (unsigned int)ctx->input.pressed[LIFT_IN_UPPER_LIMIT],
                   (unsigned int)ctx->input.pressed[LIFT_IN_PHOTO],
                   (unsigned int)ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
                   LiftIot_AlarmName(ctx),
                   (unsigned int)ctx->input.pressed[LIFT_IN_UPPER_LIMIT],
                    (unsigned int)ctx->input.pressed[LIFT_IN_ESTOP],
                    (unsigned int)ctx->input.pressed[LIFT_IN_PHOTO],
                    (unsigned int)ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
                    (unsigned long)maintenance.total_lift_count,
                    (unsigned long)maintenance.maintenance_lift_count,
                    (unsigned int)W25Q_MAINTENANCE_THRESHOLD,
                    (unsigned long)maintenance.maintenance_count,
                    (unsigned long)maintenance.last_maintenance_total,
                    (unsigned long)maintenance.maintenance_due,
                    (unsigned long)maintenance.usage_epoch);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_BuildOpLogJson(char *buf,
                                         uint16_t size,
                                         uint16_t start_index,
                                         uint16_t count,
                                         uint32_t seq)
{
    (void)start_index;
    (void)count;

    if (buf == NULL)
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    if (snprintf(buf, size,
                 "{\"type\":\"logs_batch\",\"device\":\"%s\",\"uid\":\"%s\",\"seq\":%lu,\"logs\":[],\"count\":0}",
                 LIFT_IOT_DEVICE_ID,
                 App_Product_GetUIDString(),
                 (unsigned long)seq) >= (int)size)
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_HandleClearAlarm(const char *account)
{
    uint32_t start;
    const lift_ctx_t *ctx;

    if (App_LiftCore_RequestClearPhotoAlarm((account != NULL) ? account : "remote") == 0U)
    {
        return LIFT_IOT_DENIED;
    }

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 250U)
    {
        ctx = App_LiftCore_GetContext();
        if ((ctx != NULL) &&
            (ctx->alarm.photo_alarm_latched == 0U) &&
            (ctx->current_state != STATE_PHOTO_ALARM))
        {
            g_lift_iot_status.last_command_tick = HAL_GetTick();
            g_iot_event_pending = 1U;
            return LIFT_IOT_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }

    App_LiftCore_CancelClearPhotoAlarm();
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    return LIFT_IOT_DENIED;
}

lift_iot_result_t LiftIot_HandleRemoteLock(uint8_t locked, const char *account)
{
    return LiftIot_SetLocked(locked, account);
}

lift_iot_result_t LiftIot_HandleEnterMaintenance(const char *account)
{
    (void)account;
    g_lift_iot_status.maintenance_due = 1U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_HandleExitMaintenance(const char *account)
{
    (void)account;
    g_lift_iot_status.maintenance_due = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    return LIFT_IOT_OK;
}

void LiftIot_NotifyPhotoAlarm(void)
{
    g_iot_event_pending = 1U;
    g_lift_iot_status.last_alarm_tick = HAL_GetTick();
}

void LiftIot_NotifyEstop(void)
{
    g_iot_event_pending = 1U;
    g_lift_iot_status.last_alarm_tick = HAL_GetTick();
}

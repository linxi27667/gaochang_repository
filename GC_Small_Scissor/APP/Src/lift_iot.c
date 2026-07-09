#include "lift_iot.h"

#include "app_io_map.h"
#include "app_op_log.h"
#include "app_product.h"
#include "app_w25qxx.h"
#include "elog.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static lift_iot_status_t g_lift_iot_status;
static lift_state_t g_lift_iot_last_state = STATE_IDLE;
static uint32_t g_lift_iot_motion_start_tick;
static volatile uint8_t g_iot_event_pending;

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
}

void LiftIot_Poll(void)
{
    const lift_ctx_t *ctx = App_LiftCore_GetContext();

    if (ctx == NULL)
    {
        return;
    }

    LiftIot_UpdateMotionStat(HAL_GetTick(), ctx->current_state);
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

void LiftIot_MaintenanceDone(const char *account)
{
    (void)account;
    g_lift_iot_status.maintenance_due = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
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
    int len;

    if ((buf == NULL) || (type == NULL) || (dtu_state == NULL) || (ctx == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    avg_ms = (g_lift_iot_status.run_count == 0U)
             ? 0U
             : (g_lift_iot_status.total_run_ms / g_lift_iot_status.run_count);

    len = snprintf(buf, size,
                   "{\"type\":\"%s\",\"device\":\"%s\",\"serial\":\"%s\",\"sn\":\"%s\","
                   "\"uid\":\"%s\",\"chip_uid\":\"%s\",\"name\":\"%s\",\"model\":\"%s\",\"group\":\"%s\","
                   "\"product_type\":\"small_scissor\",\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,"
                   "\"state\":\"%s\",\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,\"buzzer_on\":0,"
                   "\"io_input\":{\"btn_up\":%u,\"btn_down\":%u,\"btn_lock\":%u,\"estop\":%u,"
                   "\"upper_limit\":%u,\"lower_limit\":%u,\"btn_refill\":%u,\"photoelectric\":%u},"
                   "\"io_output\":{\"motor\":%u,\"drop_valve\":%u,\"valve_air\":%u},"
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%u,\"upper\":%u,\"lower\":%u,\"estop\":%u,\"photoelectric\":%u},"
                   "\"stats\":{\"up\":%lu,\"down\":%lu,\"lock\":%lu,\"refill\":%lu,\"estop\":%lu,\"photo_alarm\":%lu,\"total_run_ms\":%lu},"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   type,
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   App_Product_GetUIDString(),
                   App_Product_GetUIDString(),
                   LIFT_IOT_DEVICE_NAME,
                   LIFT_IOT_DEVICE_MODEL,
                   LIFT_IOT_DEVICE_GROUP,
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
                   (unsigned int)ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
                   (unsigned int)ctx->input.pressed[LIFT_IN_REFILL],
                   (unsigned int)ctx->input.pressed[LIFT_IN_PHOTO],
                   (unsigned int)ctx->output_actual.motor_on,
                   (unsigned int)ctx->output_actual.down_valve_on,
                   (unsigned int)ctx->output_actual.air_valve_on,
                   LiftIot_AlarmName(ctx),
                   (unsigned int)LiftIot_AlarmCode(ctx),
                   (unsigned int)ctx->input.pressed[LIFT_IN_UPPER_LIMIT],
                   (unsigned int)ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
                   (unsigned int)ctx->input.pressed[LIFT_IN_ESTOP],
                   (unsigned int)ctx->input.pressed[LIFT_IN_PHOTO],
                   (unsigned long)ctx->up_count,
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

lift_iot_result_t LiftIot_BuildStatusJson(char *buf,
                                          uint16_t size,
                                          const char *event,
                                          uint32_t seq,
                                          const char *dtu_state,
                                          int16_t csq)
{
    char json[1024];
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
    char json[1024];
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
    int len;

    if ((buf == NULL) || (ctx == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    len = snprintf(buf, size,
                   "{\"type\":\"height\",\"device\":\"%s\",\"serial\":\"%s\",\"sn\":\"%s\","
                   "\"uid\":\"%s\",\"chip_uid\":\"%s\",\"product_type\":\"small_scissor\","
                   "\"seq\":%lu,\"tick\":%lu,\"state\":\"%s\",\"alarm\":\"%s\","
                   "\"io_input\":{\"estop\":%u,\"upper_limit\":%u,\"lower_limit\":%u,\"photoelectric\":%u},"
                   "\"safety\":{\"alarm\":\"%s\",\"upper\":%u,\"lower\":%u,\"estop\":%u,\"photoelectric\":%u},"
                   "\"note\":\"small_scissor_no_encoder\"}",
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   App_Product_GetUIDString(),
                   App_Product_GetUIDString(),
                   (unsigned long)seq,
                   (unsigned long)HAL_GetTick(),
                   LiftIot_StateName(),
                   LiftIot_AlarmName(ctx),
                   (unsigned int)ctx->input.pressed[LIFT_IN_ESTOP],
                   (unsigned int)ctx->input.pressed[LIFT_IN_UPPER_LIMIT],
                   (unsigned int)ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
                   (unsigned int)ctx->input.pressed[LIFT_IN_PHOTO],
                   LiftIot_AlarmName(ctx),
                   (unsigned int)ctx->input.pressed[LIFT_IN_UPPER_LIMIT],
                   (unsigned int)ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
                   (unsigned int)ctx->input.pressed[LIFT_IN_ESTOP],
                   (unsigned int)ctx->input.pressed[LIFT_IN_PHOTO]);

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
    App_LiftCore_RequestClearPhotoAlarm((account != NULL) ? account : "remote");
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    return LIFT_IOT_OK;
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

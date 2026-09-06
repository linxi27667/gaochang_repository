#include "app_lift_iot.h"
#include "app_tas_dtu.h"
#include "app_w25qxx.h"
#include "encoder.h"
#include "key.h"
#include "motor.h"
#include "safety.h"
#include "elog.h"
#include <stdio.h>
#include <string.h>

/* ================= 1. 设备物联网状态对象 ================= */

static lift_iot_status_t g_lift_iot_status;
static direction_t g_lift_iot_last_direction = DIR_STOP;
static uint32_t g_lift_iot_motion_start_tick;
static uint32_t g_lift_iot_boot_tick;
static alarm_t g_lift_iot_last_alarm = ALARM_NONE;
static volatile uint8_t g_iot_event_pending;

/* ================= 2. 状态计算与统计维护 ================= */

static const char *App_LiftIot_DirectionName(direction_t direction)
{
    if (direction == DIR_UP)
    {
        return "up";
    }

    if (direction == DIR_DOWN)
    {
        return "down";
    }

    return "stop";
}

static const char *App_LiftIot_AlarmName(alarm_t alarm)
{
    switch (alarm)
    {
        case ALARM_COLLISION:
            return "collision";

        case ALARM_STALL:
            return "stall";

        case ALARM_BALANCE_TIMEOUT:
            return "balance_timeout";

        case ALARM_NONE:
        default:
            return "none";
    }
}

static uint32_t App_LiftIot_AverageRunMs(void)
{
    if (g_lift_iot_status.run_count == 0U)
    {
        return 0U;
    }

    return g_lift_iot_status.total_run_ms / g_lift_iot_status.run_count;
}

static const char *App_LiftIot_BootId(void)
{
    static char boot_id[40];

    if (boot_id[0] == '\0')
    {
        (void)snprintf(boot_id, sizeof(boot_id), "%s-%08lx",
                       App_TasDtu_ChipUidString(),
                       (unsigned long)g_lift_iot_boot_tick);
    }

    return boot_id;
}

static void App_LiftIot_UpdateMotionStat(uint32_t now)
{
    direction_t direction = g_command.direction;

    if ((g_lift_iot_last_direction == DIR_STOP) && (direction != DIR_STOP))
    {
        g_lift_iot_motion_start_tick = now;
        g_lift_iot_status.current_run_ms = 0U;
        g_lift_iot_status.run_count++;
    }
    else if ((g_lift_iot_last_direction != DIR_STOP) && (direction == DIR_STOP))
    {
        uint32_t duration = now - g_lift_iot_motion_start_tick;
        g_lift_iot_status.total_run_ms += duration;
        g_lift_iot_status.current_run_ms = 0U;
    }
    else if (direction != DIR_STOP)
    {
        g_lift_iot_status.current_run_ms = now - g_lift_iot_motion_start_tick;
    }

    g_lift_iot_last_direction = direction;
}

/* ================= 3. 事件标志与状态查询 ================= */

uint8_t App_LiftIot_PeekEventFlag(void)
{
    return g_iot_event_pending;
}

uint8_t App_LiftIot_ConsumeEventFlag(void)
{
    uint8_t old = g_iot_event_pending;
    g_iot_event_pending = 0U;
    return old;
}

uint8_t App_LiftIot_IsInMotion(void)
{
    return (g_command.direction != DIR_STOP) ? 1U : 0U;
}

/* ================= 4. 对外业务接口 ================= */

void App_LiftIot_Init(void)
{
    memset(&g_lift_iot_status, 0, sizeof(g_lift_iot_status));
    g_lift_iot_last_direction = DIR_STOP;
    g_lift_iot_boot_tick = HAL_GetTick();
    g_lift_iot_last_alarm = ALARM_NONE;
    g_iot_event_pending = 0U;
}

void App_LiftIot_Poll(void)
{
    uint32_t now = HAL_GetTick();
    direction_t prev_dir = g_lift_iot_last_direction;

    if ((g_lift_iot_status.locked != 0U) && (g_command.direction != DIR_STOP))
    {
        Motor_Stop_All_Immediate();
        g_command.direction = DIR_STOP;
        elog_w("IOT", "[IOT] locked device forced motion stop");
    }

    App_LiftIot_UpdateMotionStat(now);

    /* 事件：运动方向变化（启/停） */
    if (prev_dir != g_command.direction)
    {
        g_iot_event_pending = 1U;
    }

    if ((g_safety.alarm != ALARM_NONE) && (g_lift_iot_last_alarm != g_safety.alarm))
    {
        g_lift_iot_status.last_alarm_tick = now;
        g_iot_event_pending = 1U;
        elog_w("IOT", "[IOT] alarm event=%s", App_LiftIot_AlarmName(g_safety.alarm));
    }

    /* 报警清除也是事件（ALARM_NONE 但之前有报警） */
    if ((g_safety.alarm == ALARM_NONE) && (g_lift_iot_last_alarm != ALARM_NONE))
    {
        g_iot_event_pending = 1U;
    }

    g_lift_iot_last_alarm = g_safety.alarm;
}

uint8_t App_LiftIot_IsLocked(void)
{
    return g_lift_iot_status.locked;
}

lift_iot_result_t App_LiftIot_SetLocked(uint8_t locked, const char *source)
{
    g_lift_iot_status.locked = (locked != 0U) ? 1U : 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();

    if (g_lift_iot_status.locked != 0U)
    {
        Motor_Stop_All_Immediate();
        g_command.direction = DIR_STOP;
    }

    elog_a("IOT",
           "[IOT] %s by %s",
           (g_lift_iot_status.locked != 0U) ? "locked" : "unlocked",
           (source != NULL) ? source : "remote");

    return LIFT_IOT_OK;
}

lift_iot_result_t App_LiftIot_EnterAdmin(const char *password, const char *account)
{
    if (password == NULL)
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    if (strcmp(password, LIFT_IOT_ADMIN_PASSWORD) != 0)
    {
        elog_w("IOT", "[IOT] admin denied account=%s", (account != NULL) ? account : "unknown");
        return LIFT_IOT_DENIED;
    }

    g_lift_iot_status.admin_mode = 1U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    elog_a("IOT", "[IOT] admin mode entered account=%s", (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

void App_LiftIot_ExitAdmin(void)
{
    g_lift_iot_status.admin_mode = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    elog_a("IOT", "[IOT] admin mode exited");
}

lift_iot_result_t App_LiftIot_ClearFault(const char *account)
{
    if (g_lift_iot_status.admin_mode == 0U)
    {
        elog_w("IOT", "[IOT] fault clear denied account=%s", (account != NULL) ? account : "unknown");
        return LIFT_IOT_DENIED;
    }

    Safety_Alarm_Reset();
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    elog_a("IOT", "[IOT] fault cleared account=%s", (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

lift_iot_result_t App_LiftIot_AdminJog(uint8_t column_index,
                                       uint8_t direction_up,
                                       uint32_t duration_ms,
                                       const char *account)
{
    direction_t direction = (direction_up != 0U) ? DIR_UP : DIR_DOWN;

    if ((g_lift_iot_status.admin_mode == 0U) || (g_lift_iot_status.locked != 0U))
    {
        elog_w("IOT", "[IOT] admin jog denied account=%s", (account != NULL) ? account : "unknown");
        return LIFT_IOT_DENIED;
    }

    if (Motor_Admin_Jog(column_index, direction, duration_ms) == 0U)
    {
        elog_w("IOT", "[IOT] admin jog blocked col=%u dir=%s",
               (unsigned int)column_index,
               App_LiftIot_DirectionName(direction));
        return LIFT_IOT_DENIED;
    }

    g_lift_iot_status.last_command_tick = HAL_GetTick();
    elog_a("IOT",
           "[IOT] admin jog col=%u dir=%s duration=%lu account=%s",
           (unsigned int)column_index,
           App_LiftIot_DirectionName(direction),
           (unsigned long)duration_ms,
           (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

void App_LiftIot_MaintenanceDone(const char *account)
{
    g_lift_iot_status.maintenance_due = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    elog_a("IOT", "[IOT] maintenance done account=%s", (account != NULL) ? account : "unknown");
}

const lift_iot_status_t *App_LiftIot_GetStatus(void)
{
    return &g_lift_iot_status;
}

const char *App_LiftIot_StateName(void)
{
    if (g_safety.alarm != ALARM_NONE)
    {
        return "fault";
    }

    if (g_lift_iot_status.locked != 0U)
    {
        return "locked";
    }

    if (g_lift_iot_status.maintenance_due != 0U)
    {
        return "maintenance_due";
    }

    /* 与四款已验证机对齐：运动态用 rising/dropping，静止用 idle */
    if (g_command.direction == DIR_UP)
    {
        return "rising";
    }
    if (g_command.direction == DIR_DOWN)
    {
        return "dropping";
    }

    return "idle";
}

lift_iot_result_t App_LiftIot_BuildTelemetryJson(char *buf,
                                                 uint16_t size,
                                                 const char *type,
                                                 uint32_t seq,
                                                 const char *dtu_state,
                                                 int16_t csq)
{
    int32_t left_pulse;
    int32_t right_pulse;
    int32_t left_mm;
    int32_t right_mm;
    uint32_t now;
    int len;

    if ((buf == NULL) || (type == NULL) || (dtu_state == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    left_pulse = Encoder_Get_Count(0U);
    right_pulse = Encoder_Get_Count(1U);
    left_mm = HEIGHT_MM(left_pulse);
    right_mm = HEIGHT_MM(right_pulse);
    now = HAL_GetTick();

    len = snprintf(buf,
                   size,
                    "{\"v\":1,\"type\":\"%s\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\",\"product_type\":\"%s\","
                    "\"name\":\"%s\",\"model\":\"%s\",\"group\":\"%s\",\"firmware_version\":\"%s\",\"boot_id\":\"%s\","
                   "\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,\"state\":\"%s\",\"locked\":%u,"
                   "\"maintenance_due\":%u,\"admin_mode\":%u,"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"direction\":\"%s\","
                   "\"height\":{\"left_mm\":%ld,\"right_mm\":%ld,\"diff_mm\":%ld,"
                   "\"left_pulse\":%ld,\"right_pulse\":%ld},"
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%d,\"upper\":%u,\"lower\":%u,"
                   "\"stall\":%u,\"collision_up\":%u,\"collision_down\":%u,"
                   "\"left_up_collision\":%u,\"right_up_collision\":%u,"
                   "\"left_down_collision\":%u,\"right_down_collision\":%u},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                    type,
                    LIFT_IOT_DEVICE_ID,
                    App_TasDtu_ChipUidString(),
                    App_TasDtu_ChipUidString(),
                    LIFT_IOT_PRODUCT_TYPE,
                    LIFT_IOT_DEVICE_NAME,
                    LIFT_IOT_DEVICE_MODEL,
                    LIFT_IOT_DEVICE_GROUP,
                    LIFT_IOT_FIRMWARE_VERSION,
                    App_LiftIot_BootId(),
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   App_LiftIot_StateName(),
                   (unsigned int)g_lift_iot_status.locked,
                   (unsigned int)g_lift_iot_status.maintenance_due,
                   (unsigned int)g_lift_iot_status.admin_mode,
                   (unsigned long)g_lift_iot_status.total_run_ms,
                   (unsigned long)g_lift_iot_status.current_run_ms,
                   (unsigned long)g_lift_iot_status.run_count,
                   (unsigned long)App_LiftIot_AverageRunMs(),
                   App_LiftIot_DirectionName(g_command.direction),
                   (long)left_mm,
                   (long)right_mm,
                   (long)(left_mm - right_mm),
                   (long)left_pulse,
                   (long)right_pulse,
                   App_LiftIot_AlarmName(g_safety.alarm),
                   (int)g_safety.alarm,
                   (unsigned int)g_safety.at_upper_limit,
                   (unsigned int)g_safety.at_lower_limit,
                   (unsigned int)g_safety.stall_suspected,
                   (unsigned int)(g_safety.left_up_collision || g_safety.right_up_collision),
                   (unsigned int)(g_safety.left_down_collision || g_safety.right_down_collision),
                   (unsigned int)g_safety.left_up_collision,
                   (unsigned int)g_safety.right_up_collision,
                   (unsigned int)g_safety.left_down_collision,
                   (unsigned int)g_safety.right_down_collision,
                   dtu_state,
                   (int)csq);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t App_LiftIot_BuildStatusJson(char *buf,
                                              uint16_t size,
                                              const char *event,
                                              uint32_t seq,
                                              const char *dtu_state,
                                              int16_t csq)
{
    uint32_t now;
    int len;

    if ((buf == NULL) || (event == NULL) || (dtu_state == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    now = HAL_GetTick();

    len = snprintf(buf,
                   size,
                    "{\"v\":1,\"type\":\"status\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\",\"product_type\":\"%s\","
                    "\"name\":\"%s\",\"model\":\"%s\",\"group\":\"%s\",\"firmware_version\":\"%s\",\"boot_id\":\"%s\","
                   "\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,\"event\":\"%s\",\"state\":\"%s\","
                   "\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%d},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                    LIFT_IOT_DEVICE_ID,
                    App_TasDtu_ChipUidString(),
                    App_TasDtu_ChipUidString(),
                    LIFT_IOT_PRODUCT_TYPE,
                    LIFT_IOT_DEVICE_NAME,
                    LIFT_IOT_DEVICE_MODEL,
                    LIFT_IOT_DEVICE_GROUP,
                    LIFT_IOT_FIRMWARE_VERSION,
                    App_LiftIot_BootId(),
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   event,
                   App_LiftIot_StateName(),
                   (unsigned int)g_lift_iot_status.locked,
                   (unsigned int)g_lift_iot_status.maintenance_due,
                   (unsigned int)g_lift_iot_status.admin_mode,
                   (unsigned long)g_lift_iot_status.total_run_ms,
                   (unsigned long)g_lift_iot_status.current_run_ms,
                   (unsigned long)g_lift_iot_status.run_count,
                   (unsigned long)App_LiftIot_AverageRunMs(),
                   App_LiftIot_AlarmName(g_safety.alarm),
                   (int)g_safety.alarm,
                   dtu_state,
                   (int)csq);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t App_LiftIot_BuildCommandStatusJson(char *buf,
                                                     uint16_t size,
                                                     const char *event,
                                                     const char *cmd,
                                                     const char *msg_id,
                                                     const char *result,
                                                     uint32_t seq,
                                                     const char *dtu_state,
                                                     int16_t csq)
{
    uint32_t now;
    int len;

    if ((buf == NULL) || (event == NULL) || (cmd == NULL) ||
        (msg_id == NULL) || (result == NULL) || (dtu_state == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    /* 命令回执必须带 cmd/msg_id/result，Web 端才能把 command_queue 闭环。 */
    now = HAL_GetTick();

    len = snprintf(buf,
                   size,
                    "{\"v\":1,\"type\":\"command_response\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\",\"product_type\":\"%s\","
                    "\"name\":\"%s\",\"model\":\"%s\",\"group\":\"%s\",\"firmware_version\":\"%s\",\"boot_id\":\"%s\","
                    "\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,\"event\":\"%s\",\"cmd\":\"%s\",\"msg_id\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\","
                   "\"state\":\"%s\",\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%d},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                    LIFT_IOT_DEVICE_ID,
                    App_TasDtu_ChipUidString(),
                    App_TasDtu_ChipUidString(),
                    LIFT_IOT_PRODUCT_TYPE,
                    LIFT_IOT_DEVICE_NAME,
                    LIFT_IOT_DEVICE_MODEL,
                    LIFT_IOT_DEVICE_GROUP,
                    LIFT_IOT_FIRMWARE_VERSION,
                    App_LiftIot_BootId(),
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   event,
                   cmd,
                    msg_id,
                    result,
                    (strcmp(result, "succeeded") == 0) ? "" : event,
                   App_LiftIot_StateName(),
                   (unsigned int)g_lift_iot_status.locked,
                   (unsigned int)g_lift_iot_status.maintenance_due,
                   (unsigned int)g_lift_iot_status.admin_mode,
                   (unsigned long)g_lift_iot_status.total_run_ms,
                   (unsigned long)g_lift_iot_status.current_run_ms,
                   (unsigned long)g_lift_iot_status.run_count,
                   (unsigned long)App_LiftIot_AverageRunMs(),
                   App_LiftIot_AlarmName(g_safety.alarm),
                   (int)g_safety.alarm,
                   dtu_state,
                   (int)csq);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t App_LiftIot_BuildHeightJson(char *buf,
                                             uint16_t size,
                                             uint32_t seq)
{
    int32_t left_pulse;
    int32_t right_pulse;
    int32_t left_mm;
    int32_t right_mm;
    int len;

    if (buf == NULL)
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    left_pulse = Encoder_Get_Count(0U);
    right_pulse = Encoder_Get_Count(1U);
    left_mm = HEIGHT_MM(left_pulse);
    right_mm = HEIGHT_MM(right_pulse);

    len = snprintf(buf,
                   size,
                    "{\"v\":1,\"type\":\"height\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\",\"product_type\":\"%s\",\"firmware_version\":\"%s\",\"boot_id\":\"%s\",\"seq\":%lu,\"tick\":%lu,"
                   "\"state\":\"%s\",\"locked\":%u,\"direction\":\"%s\","
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%d},"
                   "\"height\":{\"left_mm\":%ld,\"right_mm\":%ld,\"diff_mm\":%ld,"
                   "\"left_pulse\":%ld,\"right_pulse\":%ld}}",
                    LIFT_IOT_DEVICE_ID,
                    App_TasDtu_ChipUidString(),
                    App_TasDtu_ChipUidString(),
                    LIFT_IOT_PRODUCT_TYPE,
                    LIFT_IOT_FIRMWARE_VERSION,
                   App_LiftIot_BootId(),
                   (unsigned long)seq,
                   (unsigned long)HAL_GetTick(),
                   App_LiftIot_StateName(),
                   (unsigned int)g_lift_iot_status.locked,
                   App_LiftIot_DirectionName(g_command.direction),
                   App_LiftIot_AlarmName(g_safety.alarm),
                   (int)g_safety.alarm,
                   (long)left_mm,
                   (long)right_mm,
                   (long)(left_mm - right_mm),
                   (long)left_pulse,
                   (long)right_pulse);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

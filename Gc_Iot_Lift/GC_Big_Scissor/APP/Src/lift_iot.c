#include "lift_iot.h"

#include "app_maintenance.h"
#include "app_rise_counter.h"
#include "app_w25qxx.h"
#include "lift_core.h"
#include "app_io_map.h"
#include "app_op_log.h"
#include "app_product.h"
#include "app_buzzer.h"
#include "lift_lock.h"   /* 互斥锁与快照 */
#include "elog.h"
#include <stdio.h>
#include <string.h>

/* ================= 1. 设备物联网状态对象 ================= */

static lift_iot_status_t g_lift_iot_status;
static lift_state_t g_lift_iot_last_state = LIFT_STATE_IDLE;
static uint32_t g_lift_iot_motion_start_tick;
static volatile uint8_t g_iot_event_pending;
volatile uint8_t g_app_buzzer_on = 0U;

#define STM32_UID_BASE_ADDR  (0x1FFF7A10UL)

static const char *LiftIot_ChipUidString(void)
{
    static char uid[25];
    const volatile uint32_t *uid_reg = (const volatile uint32_t *)STM32_UID_BASE_ADDR;

    if (uid[0] == '\0')
    {
        (void)snprintf(uid, sizeof(uid), "%08lx%08lx%08lx",
                       (unsigned long)uid_reg[0],
                       (unsigned long)uid_reg[1],
                       (unsigned long)uid_reg[2]);
    }

    return uid;
}

/* ================= 2. 状态计算与统计维护 ================= */

/* 判断当前是否处于运动状态（用于 telemetry 周期和运动统计） */
static uint8_t LiftIot_IsMotionState(lift_state_t state)
{
    return (state == LIFT_STATE_RISING ||
            state == LIFT_STATE_DROPPING ||
            state == LIFT_STATE_REFILLING) ? 1U : 0U;
}

static void LiftIot_UpdateMotionStat(uint32_t now)
{
    LiftLock_LockState();
    lift_state_t cur = g_lift_state;
    LiftLock_UnlockState();

    LiftLock_LockIot();
    uint8_t cur_motion  = LiftIot_IsMotionState(cur);
    uint8_t last_motion = LiftIot_IsMotionState(g_lift_iot_last_state);

    if ((!last_motion) && cur_motion)
    {
        /* 运动开始 */
        g_lift_iot_motion_start_tick = now;
        g_lift_iot_status.current_run_ms = 0U;
        g_lift_iot_status.run_count++;
    }
    else if (last_motion && (!cur_motion))
    {
        /* 运动结束 */
        uint32_t duration = now - g_lift_iot_motion_start_tick;
        g_lift_iot_status.total_run_ms += duration;
        g_lift_iot_status.current_run_ms = 0U;
    }
    else if (cur_motion)
    {
        /* 运动中 */
        g_lift_iot_status.current_run_ms = now - g_lift_iot_motion_start_tick;
    }

    g_lift_iot_last_state = cur;
    LiftLock_UnlockIot();
}

/* ================= 3. 事件标志与状态查询 ================= */

uint8_t LiftIot_PeekEventFlag(void)
{
    LiftLock_LockIot();
    uint8_t v = g_iot_event_pending;
    LiftLock_UnlockIot();
    return v;
}

uint8_t LiftIot_ConsumeEventFlag(void)
{
    LiftLock_LockIot();
    uint8_t old = g_iot_event_pending;
    g_iot_event_pending = 0U;
    LiftLock_UnlockIot();
    return old;
}

uint8_t LiftIot_IsInMotion(void)
{
    LiftLock_LockState();
    lift_state_t s = g_lift_state;
    LiftLock_UnlockState();
    return LiftIot_IsMotionState(s);
}

/* ================= 4. 对外业务接口 ================= */

void LiftIot_Init(void)
{
    app_maintenance_status_t maintenance;

    App_Maintenance_GetStatus(&maintenance);
    LiftLock_LockIot();
    memset(&g_lift_iot_status, 0, sizeof(g_lift_iot_status));
    g_lift_iot_status.maintenance_due = maintenance.maintenance_due;
    g_lift_iot_last_state = LIFT_STATE_IDLE;
    g_iot_event_pending = 0U;
    LiftLock_UnlockIot();
}

void LiftIot_Poll(void)
{
    uint32_t now = HAL_GetTick();
    app_maintenance_status_t maintenance;

    App_Maintenance_GetStatus(&maintenance);

    /* 远程锁定时强制停止运动（双保险，LiftCore 已处理） */
    LiftLock_LockIot();
    if (g_lift_iot_status.maintenance_due != maintenance.maintenance_due)
    {
        g_lift_iot_status.maintenance_due = maintenance.maintenance_due;
        g_iot_event_pending = 1U;
    }
    uint8_t iot_locked = g_lift_iot_status.locked;
    LiftLock_UnlockIot();

    LiftLock_LockState();
    uint8_t is_motion = LiftIot_IsMotionState(g_lift_state);
    LiftLock_UnlockState();

    if ((iot_locked != 0U) && is_motion)
    {
        App_IO_All_Off();
        LiftCore_SetRemoteLock(1);
        elog_w("IOT", "[IOT] locked device forced motion stop");
    }

    /* 持锁修改 g_lift_iot_last_state 与 g_lift_iot_status */
    LiftLock_LockIot();
    lift_state_t prev_state = g_lift_iot_last_state;
    LiftLock_UnlockIot();

    LiftIot_UpdateMotionStat(now);

    /* 重新持锁置事件标志 */
    LiftLock_LockIot();
    if (prev_state != g_lift_state)   /* g_lift_state 已被 UpdateMotionStat 缓存到 g_lift_iot_last_state */
    {
        g_iot_event_pending = 1U;
    }
    LiftLock_UnlockIot();
}

uint8_t LiftIot_IsLocked(void)
{
    LiftLock_LockIot();
    uint8_t v = g_lift_iot_status.locked;
    LiftLock_UnlockIot();
    return v;
}

lift_iot_result_t LiftIot_SetLocked(uint8_t locked, const char *source)
{
    LiftLock_LockIot();
    g_lift_iot_status.locked = (locked != 0U) ? 1U : 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    LiftLock_UnlockIot();

    /* 通过 LiftCore 同步远程锁定状态 */
    LiftCore_SetRemoteLock(locked);

    if (locked)
    {
        App_IO_All_Off();
    }

    elog_a("IOT",
           "[IOT] %s by %s",
           locked ? "locked" : "unlocked",
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
        elog_w("IOT", "[IOT] admin denied account=%s", (account != NULL) ? account : "unknown");
        return LIFT_IOT_DENIED;
    }

    LiftLock_LockIot();
    g_lift_iot_status.admin_mode = 1U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    LiftLock_UnlockIot();
    elog_a("IOT", "[IOT] admin mode entered account=%s", (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

void LiftIot_ExitAdmin(void)
{
    LiftLock_LockIot();
    g_lift_iot_status.admin_mode = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    LiftLock_UnlockIot();
    elog_a("IOT", "[IOT] admin mode exited");
}

lift_iot_result_t LiftIot_ClearFault(const char *account)
{
    LiftLock_LockIot();
    uint8_t in_admin = g_lift_iot_status.admin_mode;
    LiftLock_UnlockIot();

    if (in_admin == 0U)
    {
        elog_w("IOT", "[IOT] fault clear denied account=%s", (account != NULL) ? account : "unknown");
        return LIFT_IOT_DENIED;
    }

    /* 通过 LiftCore 解除光电报警 */
    LiftCore_ClearAlarm();
    LiftLock_LockIot();
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    LiftLock_UnlockIot();
    elog_a("IOT", "[IOT] fault cleared account=%s", (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

/* 旧版残片已替换为持锁版本 */

lift_iot_result_t LiftIot_AdminJog(uint8_t column_index,
                                       uint8_t direction_up,
                                       uint32_t duration_ms,
                                       const char *account)
{
    (void)column_index;
    (void)direction_up;
    (void)duration_ms;
    (void)account;

    /* 大剪无 jog 功能，直接拒绝（保留签名兼容 app_tas_dtu.c） */
    elog_w("IOT", "[IOT] admin jog denied (large_scissor no jog)");
    return LIFT_IOT_DENIED;
}

lift_iot_result_t LiftIot_MaintenanceDone(const char *account, const char *msg_id)
{
    if (App_Maintenance_Done(msg_id) != W25Q_OK)
    {
        return LIFT_IOT_DENIED;
    }

    LiftLock_LockIot();
    g_lift_iot_status.maintenance_due = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    LiftLock_UnlockIot();
    elog_a("IOT", "[IOT] maintenance done account=%s", (account != NULL) ? account : "unknown");
    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_ResetUsage(const char *account, const char *msg_id)
{
    app_maintenance_status_t maintenance;
    uint8_t in_admin;

    LiftLock_LockIot();
    in_admin = g_lift_iot_status.admin_mode;
    LiftLock_UnlockIot();
    if (in_admin == 0U)
    {
        return LIFT_IOT_DENIED;
    }

    if (App_Maintenance_ResetUsage(msg_id) != W25Q_OK)
    {
        return LIFT_IOT_DENIED;
    }

    App_Maintenance_GetStatus(&maintenance);
    if (App_W25Qxx_Stats_ResetUsage(maintenance.usage_epoch) != W25Q_OK)
    {
        return LIFT_IOT_DENIED;
    }

    App_RiseCounter_Reset();
    LiftLock_LockIot();
    g_lift_iot_status.maintenance_due = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    LiftLock_UnlockIot();
    elog_a("IOT", "[IOT] usage reset account=%s", (account != NULL) ? account : "unknown");
    return LIFT_IOT_OK;
}

/* LiftIot_GetStatus 返回静态对象的 const 指针。
 * 旧接口的语义被破坏：caller 直接读字段会与 DTU 任务并发。
 * 仍保留兼容，但所有 caller 必须改用 Snapshot。
 * 这里仍返回指针，但为安全起见持有短锁拷贝到静态缓冲（不推荐，建议迁移到 Snapshot） */
static lift_iot_status_t s_status_buf;
const lift_iot_status_t *LiftIot_GetStatus(void)
{
    LiftLock_LockIot();
    s_status_buf = g_lift_iot_status;
    LiftLock_UnlockIot();
    return &s_status_buf;
}

const char *LiftIot_StateName(void)
{
    /* 用快照，避免在格式化时与 DTU 任务并发改写 */
    lift_state_snapshot_t state_snap;
    lift_iot_snapshot_t  iot_snap;
    LiftIot_Snapshot(&state_snap, &iot_snap);

    /* 优先级：光电报警 > 急停 > 远程锁定 > 维保 > 正常状态 */
    if (state_snap.state == LIFT_STATE_PHOTO_ALARM)
    {
        return "photo_alarm";
    }

    if (state_snap.state == LIFT_STATE_ESTOP)
    {
        return "estop";
    }

    if (iot_snap.locked != 0U)
    {
        return "locked";
    }

    if (iot_snap.maintenance_due != 0U)
    {
        return "maintenance_due";
    }

    return LiftCore_StateName(state_snap.state);
}

static const char *LiftIot_AlarmName(void)
{
    LiftLock_LockState();
    lift_state_t s = g_lift_state;
    LiftLock_UnlockState();

    if (s == LIFT_STATE_PHOTO_ALARM)
    {
        return "photo_alarm";
    }

    if (s == LIFT_STATE_ESTOP)
    {
        return "estop";
    }

    return "none";
}

static uint8_t LiftIot_AlarmCode(void)
{
    LiftLock_LockState();
    lift_state_t s = g_lift_state;
    LiftLock_UnlockState();

    if (s == LIFT_STATE_PHOTO_ALARM)
    {
        return 1U;
    }

    if (s == LIFT_STATE_ESTOP)
    {
        return 2U;
    }

    return 0U;
}

/* ================= 5. Telemetry JSON 构造 ================= */

lift_iot_result_t LiftIot_BuildTelemetryJson(char *buf,
                                                 uint16_t size,
                                                 const char *type,
                                                 uint32_t seq,
                                                 const char *dtu_state,
                                                 int16_t csq)
{
    if ((buf == NULL) || (type == NULL) || (dtu_state == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    /* 按产品类型分发 */
    if (g_product_type == PRODUCT_TYPE_LARGE_SCISSOR)
    {
        return LiftIot_BuildLargeScissorTelemetry(buf, size, seq, dtu_state, csq);
    }

    /* 其他型号预留（双柱等暂未实现，返回空 telemetry） */
    int len = snprintf(buf, size,
                       "{\"type\":\"%s\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                       "\"product_type\":\"unknown\",\"seq\":%lu}",
                       type, LIFT_IOT_DEVICE_ID,
                       LiftIot_ChipUidString(),
                       LiftIot_ChipUidString(),
                       (unsigned long)seq);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_BuildLargeScissorTelemetry(char *buf,
                                                         uint16_t size,
                                                         uint32_t seq,
                                                         const char *dtu_state,
                                                         int16_t csq)
{
    uint32_t now;
    int len;

    if ((buf == NULL) || (dtu_state == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    now = HAL_GetTick();

    /* 读取所有输入状态 */
    uint8_t btn_up       = App_IO_Read(IO_IN_UP_BUTTON);
    uint8_t btn_down     = App_IO_Read(IO_IN_DOWN_BUTTON);
    uint8_t btn_lock     = App_IO_Read(IO_IN_LOCK_BUTTON);
    uint8_t estop        = App_IO_Read(IO_IN_ESTOP);
    uint8_t upper_limit  = App_IO_Read(IO_IN_UPPER_LIMIT);
    uint8_t sub_upper_limit = App_IO_Read(IO_IN_SUB_UPPER_LIMIT);
    uint8_t lower_limit  = App_IO_Read(IO_IN_LOWER_LIMIT);
    uint8_t refill       = App_IO_Read(IO_IN_REFILL_BUTTON);
    uint8_t photo        = App_IO_Read(IO_IN_PHOTOELECTRIC);
    uint8_t rotary       = App_IO_Read(IO_IN_ROTARY_SWITCH);

    /* 读取所有输出状态 */
    uint8_t out_motor     = App_IO_Read_Output(IO_OUT_MOTOR);
    uint8_t out_drop      = App_IO_Read_Output(IO_OUT_DROP_VALVE);
    uint8_t out_main_air  = App_IO_Read_Output(IO_OUT_MAIN_AIR_VALVE);
    uint8_t out_main_work = App_IO_Read_Output(IO_OUT_MAIN_WORK_VALVE);
    uint8_t out_sub_air   = App_IO_Read_Output(IO_OUT_SUB_AIR_VALVE);
    uint8_t out_sub_work  = App_IO_Read_Output(IO_OUT_SUB_WORK_VALVE);

    const char *role_str = App_Product_RoleName(g_current_role);

    /* IoT 状态快照（避免与 DTU 任务并发） */
    lift_iot_snapshot_t iot_snap;
    LiftLock_LockIot();
    iot_snap = (lift_iot_snapshot_t){
        .locked          = g_lift_iot_status.locked,
        .maintenance_due = g_lift_iot_status.maintenance_due,
        .admin_mode      = g_lift_iot_status.admin_mode,
        .run_count       = g_lift_iot_status.run_count,
        .total_run_ms    = g_lift_iot_status.total_run_ms,
        .current_run_ms  = g_lift_iot_status.current_run_ms,
        .event_pending   = g_iot_event_pending,
    };
    LiftLock_UnlockIot();

    app_maintenance_status_t maintenance;
    App_Maintenance_GetStatus(&maintenance);

    const char *state_str    = LiftIot_StateName();
    const char *alarm_str    = LiftIot_AlarmName();
    uint8_t     alarm_code   = LiftIot_AlarmCode();
    uint32_t    avg_ms       = (iot_snap.run_count == 0U) ? 0U : (iot_snap.total_run_ms / iot_snap.run_count);

    len = snprintf(buf, size,
                    "{\"type\":\"telemetry\","
                    "\"chip_uid\":\"%s\","
                    "\"product_type\":\"large_scissor\","
                    "\"seq\":%lu,\"tick\":%lu,"
                    "\"state\":\"%s\",\"rotary_switch\":\"%s\","
                    "\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,\"buzzer_on\":%u,"
                    "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,"
                    "\"maintenance_threshold\":5000,\"maintenance_count\":%lu,"
                    "\"last_maintenance_total\":%lu,\"maintenance_due\":%u,\"usage_epoch\":%lu},"
                   "\"io_input\":{"
                   "\"btn_up\":%u,\"btn_down\":%u,\"btn_lock\":%u,"
                    "\"estop\":%u,\"upper_limit\":%u,\"sub_upper_limit\":%u,\"lower_limit\":%u,"
                   "\"refill\":%u,\"photoelectric\":%u,\"rotary\":%u"
                   "},"
                   "\"io_output\":{"
                   "\"motor\":%u,\"drop_valve\":%u,"
                   "\"main_air\":%u,\"main_work\":%u,"
                   "\"sub_air\":%u,\"sub_work\":%u"
                   "},"
                   "\"safety\":{" 
                   "\"alarm\":\"%s\",\"alarm_code\":%u"
                   "},"
                   "\"stats\":{"
                   "\"up\":%lu,\"down\":%lu,\"lock\":%lu,\"refill\":%lu,"
                   "\"estop\":%lu,\"photo_alarm\":%lu,"
                    "\"boot_count\":%lu"
                    "},"
                    "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                    "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                    LiftIot_ChipUidString(),
                    (unsigned long)seq,
                    (unsigned long)now,
                    state_str,
                    role_str,
                   (unsigned int)iot_snap.locked,
                   (unsigned int)maintenance.maintenance_due,
                   (unsigned int)iot_snap.admin_mode,
                   (unsigned int)App_Buzzer_IsOn(),
                   (unsigned long)maintenance.total_lift_count,
                   (unsigned long)maintenance.maintenance_lift_count,
                   (unsigned long)maintenance.maintenance_count,
                   (unsigned long)maintenance.last_maintenance_total,
                   (unsigned int)maintenance.maintenance_due,
                   (unsigned long)maintenance.usage_epoch,
                   (unsigned int)btn_up,
                   (unsigned int)btn_down,
                   (unsigned int)btn_lock,
                   (unsigned int)estop,
                   (unsigned int)upper_limit,
                   (unsigned int)sub_upper_limit,
                    (unsigned int)lower_limit,
                   (unsigned int)refill,
                   (unsigned int)photo,
                   (unsigned int)rotary,
                   (unsigned int)out_motor,
                   (unsigned int)out_drop,
                   (unsigned int)out_main_air,
                   (unsigned int)out_main_work,
                   (unsigned int)out_sub_air,
                   (unsigned int)out_sub_work,
                   alarm_str,
                   (unsigned int)alarm_code,
                   (unsigned long)g_stats.up_count,
                   (unsigned long)g_stats.down_count,
                   (unsigned long)g_stats.lock_count,
                   (unsigned long)g_stats.refill_count,
                   (unsigned long)g_stats.estop_count,
                   (unsigned long)g_stats.photo_alarm_count,
                    (unsigned long)g_stats.boot_count,
                    (unsigned long)iot_snap.total_run_ms,
                    (unsigned long)iot_snap.current_run_ms,
                   (unsigned long)iot_snap.run_count,
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
    uint32_t now;
    int len;

    if ((buf == NULL) || (event == NULL) || (dtu_state == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    now = HAL_GetTick();

    lift_iot_snapshot_t iot_snap;
    LiftLock_LockIot();
    iot_snap = (lift_iot_snapshot_t){
        .locked          = g_lift_iot_status.locked,
        .maintenance_due = g_lift_iot_status.maintenance_due,
        .admin_mode      = g_lift_iot_status.admin_mode,
        .run_count       = g_lift_iot_status.run_count,
        .total_run_ms    = g_lift_iot_status.total_run_ms,
        .current_run_ms  = g_lift_iot_status.current_run_ms,
        .event_pending   = g_iot_event_pending,
    };
    LiftLock_UnlockIot();

    app_maintenance_status_t maintenance;
    App_Maintenance_GetStatus(&maintenance);

    const char *alarm_str  = LiftIot_AlarmName();
    uint8_t     alarm_code = LiftIot_AlarmCode();
    const char *state_str  = LiftIot_StateName();
    uint32_t    avg_ms     = (iot_snap.run_count == 0U) ? 0U : (iot_snap.total_run_ms / iot_snap.run_count);

    len = snprintf(buf, size,
                   "{\"type\":\"status\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"name\":\"%s\",\"model\":\"%s\",\"group\":\"%s\","
                   "\"product_type\":\"large_scissor\","
                   "\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,\"event\":\"%s\",\"state\":\"%s\","
                   "\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,"
                   "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,"
                   "\"maintenance_threshold\":5000,\"maintenance_count\":%lu,"
                   "\"last_maintenance_total\":%lu,\"maintenance_due\":%u,\"usage_epoch\":%lu},"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%d},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   LIFT_IOT_DEVICE_ID,
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   LIFT_IOT_DEVICE_NAME,
                   LIFT_IOT_DEVICE_MODEL,
                   LIFT_IOT_DEVICE_GROUP,
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   event,
                   state_str,
                   (unsigned int)iot_snap.locked,
                   (unsigned int)maintenance.maintenance_due,
                   (unsigned int)iot_snap.admin_mode,
                   (unsigned long)maintenance.total_lift_count,
                   (unsigned long)maintenance.maintenance_lift_count,
                   (unsigned long)maintenance.maintenance_count,
                   (unsigned long)maintenance.last_maintenance_total,
                   (unsigned int)maintenance.maintenance_due,
                   (unsigned long)maintenance.usage_epoch,
                   (unsigned long)iot_snap.total_run_ms,
                   (unsigned long)iot_snap.current_run_ms,
                   (unsigned long)iot_snap.run_count,
                   (unsigned long)avg_ms,
                   alarm_str,
                   (int)alarm_code,
                   dtu_state,
                   (int)csq);

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
    uint32_t now;
    int len;

    if ((buf == NULL) || (event == NULL) || (cmd == NULL) ||
        (msg_id == NULL) || (result == NULL) || (dtu_state == NULL))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    now = HAL_GetTick();

    lift_iot_snapshot_t iot_snap;
    LiftLock_LockIot();
    iot_snap = (lift_iot_snapshot_t){
        .locked          = g_lift_iot_status.locked,
        .maintenance_due = g_lift_iot_status.maintenance_due,
        .admin_mode      = g_lift_iot_status.admin_mode,
        .run_count       = g_lift_iot_status.run_count,
        .total_run_ms    = g_lift_iot_status.total_run_ms,
        .current_run_ms  = g_lift_iot_status.current_run_ms,
        .event_pending   = g_iot_event_pending,
    };
    LiftLock_UnlockIot();

    app_maintenance_status_t maintenance;
    App_Maintenance_GetStatus(&maintenance);

    const char *alarm_str  = LiftIot_AlarmName();
    uint8_t     alarm_code = LiftIot_AlarmCode();
    const char *state_str  = LiftIot_StateName();
    uint32_t    avg_ms     = (iot_snap.run_count == 0U) ? 0U : (iot_snap.total_run_ms / iot_snap.run_count);

    len = snprintf(buf, size,
                   "{\"type\":\"status\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"name\":\"%s\",\"model\":\"%s\",\"group\":\"%s\","
                   "\"product_type\":\"large_scissor\","
                   "\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,\"event\":\"%s\","
                   "\"cmd\":\"%s\",\"msg_id\":\"%s\",\"result\":\"%s\","
                   "\"state\":\"%s\",\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,"
                   "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,"
                   "\"maintenance_threshold\":5000,\"maintenance_count\":%lu,"
                   "\"last_maintenance_total\":%lu,\"maintenance_due\":%u,\"usage_epoch\":%lu},"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%d},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   LIFT_IOT_DEVICE_ID,
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   LIFT_IOT_DEVICE_NAME,
                   LIFT_IOT_DEVICE_MODEL,
                   LIFT_IOT_DEVICE_GROUP,
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   event,
                   cmd,
                   msg_id,
                   result,
                   state_str,
                   (unsigned int)iot_snap.locked,
                   (unsigned int)maintenance.maintenance_due,
                   (unsigned int)iot_snap.admin_mode,
                   (unsigned long)maintenance.total_lift_count,
                   (unsigned long)maintenance.maintenance_lift_count,
                   (unsigned long)maintenance.maintenance_count,
                   (unsigned long)maintenance.last_maintenance_total,
                   (unsigned int)maintenance.maintenance_due,
                   (unsigned long)maintenance.usage_epoch,
                   (unsigned long)iot_snap.total_run_ms,
                   (unsigned long)iot_snap.current_run_ms,
                   (unsigned long)iot_snap.run_count,
                   (unsigned long)avg_ms,
                   alarm_str,
                   (int)alarm_code,
                   dtu_state,
                   (int)csq);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_BuildHeightJson(char *buf,
                                             uint16_t size,
                                             uint32_t seq)
{
    int len;

    if (buf == NULL)
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    uint8_t estop = App_IO_Read(IO_IN_ESTOP);
    uint8_t upper_limit = App_IO_Read(IO_IN_UPPER_LIMIT);
    uint8_t sub_upper_limit = App_IO_Read(IO_IN_SUB_UPPER_LIMIT);
    uint8_t lower_limit = App_IO_Read(IO_IN_LOWER_LIMIT);
    uint8_t photo = App_IO_Read(IO_IN_PHOTOELECTRIC);
    const char *state_str = LiftIot_StateName();
    const char *alarm_str = LiftIot_AlarmName();

    len = snprintf(buf, size,
                   "{\"type\":\"height\",\"device\":\"%s\",\"serial\":\"%s\",\"sn\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"product_type\":\"large_scissor\",\"seq\":%lu,\"tick\":%lu,"
                   "\"state\":\"%s\",\"alarm\":\"%s\",\"buzzer_on\":%u,"
                   "\"io_input\":{\"estop\":%u,\"upper_limit\":%u,\"sub_upper_limit\":%u,"
                    "\"lower_limit\":%u,\"photoelectric\":%u},"
                   "\"safety\":{\"alarm\":\"%s\",\"upper\":%u,\"lower\":%u,\"estop\":%u,\"photoelectric\":%u},"
                   "\"note\":\"large_scissor_no_encoder\"}",
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   LIFT_IOT_DEVICE_ID,
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   (unsigned long)seq,
                   (unsigned long)HAL_GetTick(),
                   state_str,
                   alarm_str,
                   (unsigned int)App_Buzzer_IsOn(),
                   (unsigned int)estop,
                   (unsigned int)upper_limit,
                   (unsigned int)sub_upper_limit,
                    (unsigned int)lower_limit,
                   (unsigned int)photo,
                   alarm_str,
                   (unsigned int)upper_limit,
                   (unsigned int)lower_limit,
                   (unsigned int)estop,
                   (unsigned int)photo);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

/* ================= 6. 操作日志批量上传 JSON ================= */

lift_iot_result_t LiftIot_BuildOpLogJson(char *buf,
                                             uint16_t size,
                                             uint16_t start_index,
                                             uint16_t count,
                                             uint32_t seq)
{
    uint16_t total = App_OpLog_Count();
    uint16_t i;
    uint16_t written = 0;
    int pos = 0;
    int ret;

    if (buf == NULL)
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    /* 起始 JSON 头 */
    ret = snprintf(buf + pos, size - pos,
                   "{\"type\":\"logs_batch\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"seq\":%lu,\"tick\":%lu,\"logs\":[",
                   LIFT_IOT_DEVICE_ID,
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   (unsigned long)seq,
                   (unsigned long)HAL_GetTick());
    if (ret < 0 || ret >= (int)(size - pos))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }
    pos += ret;

    /* 循环读取日志，单帧最多 count 条（受缓冲区大小限制） */
    for (i = 0; i < count && (start_index + i) < total; i++)
    {
        w25q_op_log_entry_t entry;
        if (App_OpLog_Read(start_index + i, &entry) != 0U)
        {
            break;
        }

        /* detail 转 hex 字符串（最多 8 字节 = 16 hex 字符） */
        char detail_hex[17] = {0};
        uint8_t dlen = 8;
        for (uint8_t j = 0; j < dlen; j++)
        {
            snprintf(detail_hex + j * 2, 3, "%02X", entry.detail[j]);
        }

        ret = snprintf(buf + pos, size - pos,
                       "%s{\"ts\":%lu,\"op_type\":%u,\"result\":%u,\"duration_ms\":%u,\"detail\":\"%s\"}",
                       (written > 0) ? "," : "",
                       (unsigned long)entry.timestamp,
                       (unsigned int)entry.op_type,
                       (unsigned int)entry.op_result,
                       (unsigned int)entry.duration_ms,
                       detail_hex);
        if (ret < 0 || ret >= (int)(size - pos))
        {
            /* 缓冲区不足，停止添加 */
            break;
        }
        pos += ret;
        written++;
    }

    /* 结束 JSON */
    ret = snprintf(buf + pos, size - pos,
                   "],\"count\":%u,\"start_index\":%u}",
                   (unsigned int)written,
                   (unsigned int)start_index);
    if (ret < 0 || ret >= (int)(size - pos))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

/* ================= 7. 远程命令处理（新入口） ================= */

lift_iot_result_t LiftIot_HandleClearAlarm(const char *account)
{
    LiftCore_ClearAlarm();
    LiftLock_LockIot();
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;  /* 触发 telemetry 上报 */
    LiftLock_UnlockIot();

    elog_a("IOT", "[IOT] handle clear_alarm account=%s",
           (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_HandleRemoteLock(uint8_t locked, const char *account)
{
    LiftCore_SetRemoteLock(locked);
    LiftLock_LockIot();
    g_lift_iot_status.locked = locked ? 1U : 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    LiftLock_UnlockIot();

    if (locked)
    {
        App_IO_All_Off();
    }

    elog_a("IOT", "[IOT] handle remote_%s account=%s",
           locked ? "lock" : "unlock",
           (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_HandleEnterMaintenance(const char *account)
{
    LiftLock_LockIot();
    g_lift_iot_status.maintenance_due = 1U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    LiftLock_UnlockIot();

    elog_a("IOT", "[IOT] handle enter_maintenance account=%s",
           (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_HandleExitMaintenance(const char *account)
{
    LiftLock_LockIot();
    g_lift_iot_status.maintenance_due = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;
    LiftLock_UnlockIot();

    elog_a("IOT", "[IOT] handle exit_maintenance account=%s",
           (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

/* ================= 8. 事件上报钩子 ================= */

void LiftIot_NotifyPhotoAlarm(void)
{
    LiftLock_LockIot();
    g_iot_event_pending = 1U;
    g_lift_iot_status.last_alarm_tick = HAL_GetTick();
    LiftLock_UnlockIot();
    elog_w("IOT", "[IOT] photoelectric alarm notified");
}

void LiftIot_NotifyEstop(void)
{
    LiftLock_LockIot();
    g_iot_event_pending = 1U;
    g_lift_iot_status.last_alarm_tick = HAL_GetTick();
    LiftLock_UnlockIot();
    elog_w("IOT", "[IOT] estop event notified");
}

/* ================= 9. 快照接口 ================= */

void LiftIot_Snapshot(lift_state_snapshot_t *out_state,
                      lift_iot_snapshot_t  *out_iot)
{
    if (out_state == NULL || out_iot == NULL)
    {
        return;
    }

    /* 顺序：先 state 后 iot（与代码中其它地方获取顺序一致，避免潜在反转） */
    LiftLock_LockState();
    out_state->state         = g_lift_state;
    out_state->remote_locked = (uint8_t)(s_remote_locked ? 1U : 0U);
    LiftLock_UnlockState();

    LiftLock_LockIot();
    out_iot->locked          = g_lift_iot_status.locked;
    out_iot->maintenance_due = g_lift_iot_status.maintenance_due;
    out_iot->admin_mode      = g_lift_iot_status.admin_mode;
    out_iot->run_count       = g_lift_iot_status.run_count;
    out_iot->total_run_ms    = g_lift_iot_status.total_run_ms;
    out_iot->current_run_ms  = g_lift_iot_status.current_run_ms;
    out_iot->event_pending   = g_iot_event_pending;
    LiftLock_UnlockIot();
}

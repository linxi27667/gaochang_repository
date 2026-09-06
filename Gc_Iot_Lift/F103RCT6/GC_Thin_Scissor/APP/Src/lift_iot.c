#include "lift_iot.h"
#include "app_w25qxx.h"
#include "lift_core.h"
#include "app_io_map.h"
#include "app_op_log.h"
#include "app_product.h"
#include "lift_lock.h"   /* 浜掓枼閿佷笌蹇収 */
#include "elog.h"
#include <stdio.h>
#include <string.h>

/* ================= 1. 璁惧鐗╄仈缃戠姸鎬佸璞?================= */

static lift_iot_status_t g_lift_iot_status;
static lift_state_t g_lift_iot_last_state = LIFT_STATE_IDLE;
static uint32_t g_lift_iot_motion_start_tick;
static volatile uint8_t g_iot_event_pending;
static uint8_t g_lift_iot_rise_active;
static uint8_t g_lift_iot_rise_persist_dirty;
static uint8_t g_lift_iot_maintenance_persist_dirty;
static uint32_t g_lift_iot_rise_last_tick;
static uint32_t g_lift_iot_rise_last_save_retry_tick;
static uint8_t g_lift_iot_rise_report_inflight;
static uint32_t g_lift_iot_rise_report_count;
static uint32_t g_lift_iot_rise_report_total;
static uint32_t g_lift_iot_rise_report_sequence;
static uint16_t g_lift_iot_rise_report_remainder_ms;
static uint32_t g_lift_iot_rise_report_usage_epoch;

#define STM32_UID_BASE_ADDR  (0x1FFFF7E8UL)
#define LIFT_IOT_RISE_COUNT_PERIOD_MS       3000U
#define LIFT_IOT_RISE_SAVE_RETRY_PERIOD_MS  1000U

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

/* ================= 2. 鐘舵€佽绠椾笌缁熻缁存姢 ================= */

/* 鍒ゆ柇褰撳墠鏄惁澶勪簬杩愬姩鐘舵€侊紙鐢ㄤ簬 telemetry 鍛ㄦ湡鍜岃繍鍔ㄧ粺璁★級 */
static uint8_t LiftIot_IsMotionState(lift_state_t state)
{
    return (state == LIFT_STATE_RISING ||
            state == LIFT_STATE_DROPPING ||
            state == LIFT_STATE_REFILLING) ? 1U : 0U;
}

static uint8_t LiftIot_IsRiseCountState(lift_state_t state)
{
    if (state == LIFT_STATE_RISING)
    {
        return 1U;
    }

    return ((state == LIFT_STATE_REFILLING) &&
            (App_IO_Read(IO_IN_UP_BUTTON) != 0U) &&
            (App_IO_Read(IO_IN_REFILL_BUTTON) != 0U)) ? 1U : 0U;
}

static uint8_t LiftIot_SaveRiseStatsLocked(uint32_t now)
{
    if (g_lift_iot_maintenance_persist_dirty != 0U)
    {
        if (App_W25Qxx_Maintenance_Save() != W25Q_OK)
        {
            g_lift_iot_rise_persist_dirty = 1U;
            g_lift_iot_rise_last_save_retry_tick = now;
            elog_w("IOT", "[IOT] maintenance W25Q save failed");
            return 0U;
        }
        g_lift_iot_maintenance_persist_dirty = 0U;
    }

    if (App_W25Qxx_RiseStats_Save() != W25Q_OK)
    {
        g_lift_iot_rise_persist_dirty = 1U;
        g_lift_iot_rise_last_save_retry_tick = now;
        elog_w("IOT", "[IOT] rise stats W25Q save failed");
        return 0U;
    }

    g_lift_iot_rise_persist_dirty = 0U;
    return 1U;
}

static void LiftIot_UpdateRiseStats(uint32_t now)
{
    lift_state_t state;
    uint8_t counting;
    uint8_t need_save = 0U;

    if (g_product_type != PRODUCT_TYPE_THIN_SCISSOR)
    {
        g_lift_iot_rise_active = 0U;
        return;
    }

    LiftLock_LockState();
    state = g_lift_state;
    LiftLock_UnlockState();
    counting = LiftIot_IsRiseCountState(state);

    LiftLock_LockIot();
    if ((g_lift_iot_rise_active == 0U) && (counting != 0U))
    {
        g_lift_iot_rise_active = 1U;
        g_lift_iot_rise_last_tick = now;
    }
    else if (g_lift_iot_rise_active != 0U)
    {
        uint32_t elapsed = now - g_lift_iot_rise_last_tick;

        if (elapsed != 0U)
        {
            uint32_t accumulated = (uint32_t)g_rise_stats.remainder_ms + elapsed;
            uint32_t increments = accumulated / LIFT_IOT_RISE_COUNT_PERIOD_MS;

            g_rise_stats.remainder_ms =
                (uint16_t)(accumulated % LIFT_IOT_RISE_COUNT_PERIOD_MS);
            g_lift_iot_rise_last_tick = now;

            if (increments != 0U)
            {
                if ((UINT32_MAX - g_rise_stats.total_up_count) < increments)
                {
                    g_rise_stats.total_up_count = UINT32_MAX;
                }
                else
                {
                    g_rise_stats.total_up_count += increments;
                }

                if ((UINT32_MAX - g_rise_stats.pending_count) < increments)
                {
                    g_rise_stats.pending_count = UINT32_MAX;
                }
                else
                {
                    g_rise_stats.pending_count += increments;
                }

                App_W25Qxx_Maintenance_AddLiftUnits(increments);
                g_lift_iot_status.maintenance_due = g_maintenance.maintenance_due;
                g_lift_iot_maintenance_persist_dirty = 1U;

                g_iot_event_pending = 1U;
                need_save = 1U;
                elog_i("IOT", "[IOT] rise +%lu, total=%lu pending=%lu",
                       (unsigned long)increments,
                       (unsigned long)g_rise_stats.total_up_count,
                       (unsigned long)g_rise_stats.pending_count);
            }
        }

        if (counting == 0U)
        {
            g_lift_iot_rise_active = 0U;
            need_save = 1U;
        }
    }

    if (need_save != 0U)
    {
        (void)LiftIot_SaveRiseStatsLocked(now);
    }
    else if (((g_lift_iot_rise_persist_dirty != 0U) ||
              (g_lift_iot_maintenance_persist_dirty != 0U)) &&
             ((now - g_lift_iot_rise_last_save_retry_tick) >=
              LIFT_IOT_RISE_SAVE_RETRY_PERIOD_MS))
    {
        (void)LiftIot_SaveRiseStatsLocked(now);
    }
    LiftLock_UnlockIot();
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
        /* 杩愬姩寮€濮?*/
        g_lift_iot_motion_start_tick = now;
        g_lift_iot_status.current_run_ms = 0U;
        g_lift_iot_status.run_count++;
    }
    else if (last_motion && (!cur_motion))
    {
        /* 杩愬姩缁撴潫 */
        uint32_t duration = now - g_lift_iot_motion_start_tick;
        g_lift_iot_status.total_run_ms += duration;
        g_lift_iot_status.current_run_ms = 0U;
    }
    else if (cur_motion)
    {
        /* 杩愬姩涓?*/
        g_lift_iot_status.current_run_ms = now - g_lift_iot_motion_start_tick;
    }

    g_lift_iot_last_state = cur;
    LiftLock_UnlockIot();
}

/* ================= 3. 浜嬩欢鏍囧織涓庣姸鎬佹煡璇?================= */

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

/* ================= 4. 瀵瑰涓氬姟鎺ュ彛 ================= */

uint8_t LiftIot_HasPendingRiseReport(void)
{
    uint8_t pending;

    LiftLock_LockIot();
    pending = ((g_lift_iot_rise_persist_dirty == 0U) &&
               (g_lift_iot_maintenance_persist_dirty == 0U) &&
               (g_rise_stats.pending_count != 0U)) ? 1U : 0U;
    LiftLock_UnlockIot();

    return pending;
}

lift_iot_result_t LiftIot_BuildRiseCountJson(char *buf, uint16_t size)
{
    uint32_t total;
    uint32_t pending;
    uint32_t sequence;
    uint16_t remainder_ms;
    lift_iot_maintenance_snapshot_t maintenance;
    int len;

    if ((buf == NULL) || (size == 0U))
    {
        return LIFT_IOT_PARAM_ERROR;
    }

    LiftLock_LockIot();
    if ((g_lift_iot_rise_persist_dirty != 0U) ||
        (g_lift_iot_maintenance_persist_dirty != 0U) ||
        (g_rise_stats.pending_count == 0U))
    {
        LiftLock_UnlockIot();
        return LIFT_IOT_DENIED;
    }

    if (g_lift_iot_rise_report_inflight == 0U)
    {
        g_lift_iot_rise_report_inflight = 1U;
        g_lift_iot_rise_report_count = g_rise_stats.pending_count;
        g_lift_iot_rise_report_total = g_rise_stats.total_up_count;
        g_lift_iot_rise_report_sequence = g_rise_stats.next_sequence;
        g_lift_iot_rise_report_remainder_ms = g_rise_stats.remainder_ms;
        g_lift_iot_rise_report_usage_epoch = g_maintenance.usage_epoch;
    }

    total = g_lift_iot_rise_report_total;
    pending = g_lift_iot_rise_report_count;
    sequence = g_lift_iot_rise_report_sequence;
    remainder_ms = g_lift_iot_rise_report_remainder_ms;
    maintenance.total_lift_count = g_maintenance.total_lift_count;
    maintenance.maintenance_lift_count = g_maintenance.maintenance_lift_count;
    maintenance.maintenance_count = g_maintenance.maintenance_count;
    maintenance.last_maintenance_total = g_maintenance.last_maintenance_total;
    maintenance.maintenance_due = g_maintenance.maintenance_due;
    maintenance.usage_epoch = g_lift_iot_rise_report_usage_epoch;
    LiftLock_UnlockIot();

    len = snprintf(buf, size,
                   "{\"type\":\"rise_count\","
                   "\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"product_type\":\"thin_scissor\","
                   "\"rise_seq\":%lu,\"up_count\":%lu,"
                   "\"delta\":%lu,\"remainder_ms\":%u,\"usage_epoch\":%lu,"
                   "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,\"maintenance_threshold\":%u,\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,\"maintenance_due\":%u,\"usage_epoch\":%lu}}",
                   LIFT_IOT_DEVICE_ID,
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   (unsigned long)sequence,
                   (unsigned long)total,
                   (unsigned long)pending,
                   (unsigned int)remainder_ms,
                   (unsigned long)g_lift_iot_rise_report_usage_epoch,
                   (unsigned long)maintenance.total_lift_count,
                   (unsigned long)maintenance.maintenance_lift_count,
                   (unsigned int)W25Q_MAINTENANCE_THRESHOLD,
                   (unsigned long)maintenance.maintenance_count,
                   (unsigned long)maintenance.last_maintenance_total,
                   (unsigned int)maintenance.maintenance_due,
                   (unsigned long)maintenance.usage_epoch);
    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

uint8_t LiftIot_ConfirmPendingRiseReport(void)
{
    uint32_t old_pending;
    uint32_t old_sequence;
    uint8_t saved;

    LiftLock_LockIot();
    if ((g_lift_iot_rise_report_inflight == 0U) ||
        (g_lift_iot_rise_report_count == 0U) ||
        (g_rise_stats.pending_count < g_lift_iot_rise_report_count) ||
        (g_rise_stats.next_sequence != g_lift_iot_rise_report_sequence))
    {
        LiftLock_UnlockIot();
        return 0U;
    }

    old_pending = g_rise_stats.pending_count;
    old_sequence = g_rise_stats.next_sequence;
    g_rise_stats.pending_count -= g_lift_iot_rise_report_count;
    g_rise_stats.next_sequence++;
    if (g_rise_stats.next_sequence == 0U)
    {
        g_rise_stats.next_sequence = 1U;
    }

    saved = LiftIot_SaveRiseStatsLocked(HAL_GetTick());
    if (saved == 0U)
    {
        g_rise_stats.pending_count = old_pending;
        g_rise_stats.next_sequence = old_sequence;
        LiftLock_UnlockIot();
        return 0U;
    }

    g_lift_iot_rise_report_inflight = 0U;
    g_lift_iot_rise_report_count = 0U;
    LiftLock_UnlockIot();

    return 1U;
}

void LiftIot_Init(void)
{
    LiftLock_LockIot();
    memset(&g_lift_iot_status, 0, sizeof(g_lift_iot_status));
    g_lift_iot_last_state = LIFT_STATE_IDLE;
    g_iot_event_pending = 0U;
    g_lift_iot_rise_active = 0U;
    g_lift_iot_rise_persist_dirty = 0U;
    g_lift_iot_maintenance_persist_dirty = 0U;
    g_lift_iot_rise_last_tick = HAL_GetTick();
    g_lift_iot_rise_last_save_retry_tick = g_lift_iot_rise_last_tick;
    g_lift_iot_rise_report_inflight = 0U;
    g_lift_iot_rise_report_count = 0U;
    g_lift_iot_status.maintenance_due = g_maintenance.maintenance_due;
    LiftLock_UnlockIot();
}

void LiftIot_Poll(void)
{
    uint32_t now = HAL_GetTick();

    /* 杩滅▼閿佸畾鏃跺己鍒跺仠姝㈣繍鍔紙鍙屼繚闄╋紝LiftCore 宸插鐞嗭級 */
    LiftLock_LockIot();
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

    /* 鎸侀攣淇敼 g_lift_iot_last_state 涓?g_lift_iot_status */
    LiftLock_LockIot();
    lift_state_t prev_state = g_lift_iot_last_state;
    LiftLock_UnlockIot();

    LiftIot_UpdateRiseStats(now);
    LiftIot_UpdateMotionStat(now);

    /* 閲嶆柊鎸侀攣缃簨浠舵爣蹇?*/
    LiftLock_LockIot();
    if (prev_state != g_lift_state)   /* g_lift_state 宸茶 UpdateMotionStat 缂撳瓨鍒?g_lift_iot_last_state */
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

    /* 閫氳繃 LiftCore 鍚屾杩滅▼閿佸畾鐘舵€?*/
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

    /* 閫氳繃 LiftCore 瑙ｉ櫎鍏夌數鎶ヨ */
    LiftCore_ClearAlarm();
    LiftLock_LockIot();
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    LiftLock_UnlockIot();
    elog_a("IOT", "[IOT] fault cleared account=%s", (account != NULL) ? account : "unknown");

    return LIFT_IOT_OK;
}

/* 鏃х増娈嬬墖宸叉浛鎹负鎸侀攣鐗堟湰 */

lift_iot_result_t LiftIot_AdminJog(uint8_t column_index,
                                       uint8_t direction_up,
                                       uint32_t duration_ms,
                                       const char *account)
{
    (void)column_index;
    (void)direction_up;
    (void)duration_ms;
    (void)account;

    /* 澶у壀鏃?jog 鍔熻兘锛岀洿鎺ユ嫆缁濓紙淇濈暀绛惧悕鍏煎 app_tas_dtu.c锛?*/
    elog_w("IOT", "[IOT] admin jog denied (large_scissor no jog)");
    return LIFT_IOT_DENIED;
}

lift_iot_result_t LiftIot_MaintenanceDone(const char *account, const char *msg_id)
{
    w25q_maintenance_t saved;

    LiftLock_LockIot();
    if (App_W25Qxx_Maintenance_Done(msg_id, &saved) != W25Q_OK)
    {
        LiftLock_UnlockIot();
        elog_w("IOT", "[IOT] maintenance confirmation save failed");
        return LIFT_IOT_DENIED;
    }
    g_maintenance = saved;
    g_lift_iot_status.maintenance_due = g_maintenance.maintenance_due;
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    LiftLock_UnlockIot();
    elog_a("IOT", "[IOT] maintenance done account=%s", (account != NULL) ? account : "unknown");
    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_ResetUsage(const char *account, const char *msg_id)
{
    w25q_maintenance_t saved;

    LiftLock_LockIot();
    if (App_W25Qxx_Maintenance_ResetUsage(msg_id, &saved) != W25Q_OK)
    {
        LiftLock_UnlockIot();
        elog_w("IOT", "[IOT] usage reset maintenance save failed");
        return LIFT_IOT_DENIED;
    }
    g_maintenance = saved;

    g_rise_stats.total_up_count = 0U;
    g_rise_stats.remainder_ms = 0U;
    g_rise_stats.pending_count = 0U;
    g_rise_stats.next_sequence = 1U;
    g_lift_iot_rise_report_inflight = 0U;
    g_lift_iot_rise_report_count = 0U;
    g_lift_iot_rise_report_total = 0U;
    g_lift_iot_rise_report_sequence = 0U;
    g_lift_iot_rise_report_remainder_ms = 0U;
    g_lift_iot_rise_report_usage_epoch = g_maintenance.usage_epoch;
    g_iot_event_pending = 1U;
    g_lift_iot_status.maintenance_due = 0U;
    g_lift_iot_status.last_command_tick = HAL_GetTick();

    if (App_W25Qxx_RiseStats_Save() != W25Q_OK)
    {
        /* Keep the committed epoch; startup will clear the old rise journal. */
        g_lift_iot_rise_persist_dirty = 1U;
        g_lift_iot_rise_last_save_retry_tick = HAL_GetTick();
        LiftLock_UnlockIot();
        elog_w("IOT", "[IOT] usage reset rise save failed");
        return LIFT_IOT_DENIED;
    }

    LiftLock_UnlockIot();
    elog_a("IOT", "[IOT] usage reset account=%s", (account != NULL) ? account : "unknown");
    return LIFT_IOT_OK;
}

/* LiftIot_GetStatus 杩斿洖闈欐€佸璞＄殑 const 鎸囬拡銆? * 鏃ф帴鍙ｇ殑璇箟琚牬鍧忥細caller 鐩存帴璇诲瓧娈典細涓?DTU 浠诲姟骞跺彂銆? * 浠嶄繚鐣欏吋瀹癸紝浣嗘墍鏈?caller 蹇呴』鏀圭敤 Snapshot銆? * 杩欓噷浠嶈繑鍥炴寚閽堬紝浣嗕负瀹夊叏璧疯鎸佹湁鐭攣鎷疯礉鍒伴潤鎬佺紦鍐诧紙涓嶆帹鑽愶紝寤鸿杩佺Щ鍒?Snapshot锛?*/
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
    /* 鐢ㄥ揩鐓э紝閬垮厤鍦ㄦ牸寮忓寲鏃朵笌 DTU 浠诲姟骞跺彂鏀瑰啓 */
    lift_state_snapshot_t state_snap;
    lift_iot_snapshot_t  iot_snap;
    LiftIot_Snapshot(&state_snap, &iot_snap);

    /* 浼樺厛绾э細鍏夌數鎶ヨ > 鎬ュ仠 > 杩滅▼閿佸畾 > 缁翠繚 > 姝ｅ父鐘舵€?*/
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

/* ================= 5. Telemetry JSON 鏋勯€?================= */

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

    /* 鎸変骇鍝佺被鍨嬪垎鍙?*/
    if (g_product_type == PRODUCT_TYPE_LARGE_SCISSOR)
    {
        return LiftIot_BuildLargeScissorTelemetry(buf, size, seq, dtu_state, csq);
    }

    if (g_product_type == PRODUCT_TYPE_THIN_SCISSOR)
    {
        return LiftIot_BuildThinScissorTelemetry(buf, size, seq, dtu_state, csq);
    }

    /* 鍏朵粬鍨嬪彿棰勭暀锛堝弻鏌辩瓑鏆傛湭瀹炵幇锛岃繑鍥炵┖ telemetry锛?*/
    lift_iot_maintenance_snapshot_t maintenance;
    LiftIot_SnapshotMaintenance(&maintenance);
    int len = snprintf(buf, size,
                        "{\"type\":\"%s\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                        "\"product_type\":\"unknown\",\"seq\":%lu,"
                        "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,\"maintenance_threshold\":%u,\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,\"maintenance_due\":%u,\"usage_epoch\":%lu}}",
                       type, LIFT_IOT_DEVICE_ID,
                       LiftIot_ChipUidString(),
                       LiftIot_ChipUidString(),
                        (unsigned long)seq,
                        (unsigned long)maintenance.total_lift_count,
                        (unsigned long)maintenance.maintenance_lift_count,
                        (unsigned int)W25Q_MAINTENANCE_THRESHOLD,
                        (unsigned long)maintenance.maintenance_count,
                        (unsigned long)maintenance.last_maintenance_total,
                        (unsigned int)maintenance.maintenance_due,
                        (unsigned long)maintenance.usage_epoch);

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

lift_iot_result_t LiftIot_BuildThinScissorTelemetry(char *buf,
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

    uint8_t btn_up      = App_IO_Read(IO_IN_UP_BUTTON);
    uint8_t btn_down    = App_IO_Read(IO_IN_DOWN_BUTTON);
    uint8_t btn_lock    = App_IO_Read(IO_IN_LOCK_BUTTON);
    uint8_t estop       = App_IO_Read(IO_IN_ESTOP);
    uint8_t upper_limit = App_IO_Read(IO_IN_UPPER_LIMIT);
    uint8_t lower_limit = App_IO_Read(IO_IN_LOWER_LIMIT);
    uint8_t refill      = App_IO_Read(IO_IN_REFILL_BUTTON);
    uint8_t photo       = App_IO_Read(IO_IN_PHOTOELECTRIC);

    uint8_t out_motor = App_IO_Read_Output(IO_OUT_MOTOR);
    uint8_t out_drop  = App_IO_Read_Output(IO_OUT_DROP_VALVE);
    uint8_t out_air   = App_IO_Read_Output(IO_OUT_MAIN_AIR_VALVE);

    lift_iot_snapshot_t iot_snap;
    lift_iot_maintenance_snapshot_t maintenance;
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
    LiftIot_SnapshotMaintenance(&maintenance);

    const char *state_str = LiftIot_StateName();
    const char *alarm_str = LiftIot_AlarmName();
    uint8_t alarm_code = LiftIot_AlarmCode();

    len = snprintf(buf, size,
                   "{\"type\":\"telemetry\","
                   "\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"product_type\":\"thin_scissor\","
                   "\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,"
                   "\"state\":\"%s\","
                    "\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,"
                    "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,\"maintenance_threshold\":%u,\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,\"maintenance_due\":%u,\"usage_epoch\":%lu},"
                   "\"io_input\":{"
                   "\"btn_up\":%u,\"btn_down\":%u,\"btn_lock\":%u,"
                   "\"estop\":%u,\"upper_limit\":%u,\"lower_limit\":%u,"
                   "\"refill\":%u,\"photoelectric\":%u"
                   "},"
                   "\"io_output\":{"
                   "\"motor\":%u,\"drop_valve\":%u,\"air_valve\":%u"
                   "},"
                   "\"safety\":{"
                   "\"alarm\":\"%s\",\"alarm_code\":%u,"
                   "\"upper\":%u,\"lower\":%u,\"estop\":%u,\"photoelectric\":%u"
                   "},"
                   "\"stats\":{"
                   "\"up\":%lu,\"down\":%lu,\"lock\":%lu,\"refill\":%lu,"
                   "\"estop\":%lu,\"photo_alarm\":%lu,"
                   "\"boot_count\":%lu"
                   "},"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   state_str,
                   (unsigned int)iot_snap.locked,
                    (unsigned int)iot_snap.maintenance_due,
                    (unsigned int)iot_snap.admin_mode,
                    (unsigned long)maintenance.total_lift_count,
                    (unsigned long)maintenance.maintenance_lift_count,
                    (unsigned int)W25Q_MAINTENANCE_THRESHOLD,
                    (unsigned long)maintenance.maintenance_count,
                    (unsigned long)maintenance.last_maintenance_total,
                    (unsigned int)maintenance.maintenance_due,
                    (unsigned long)maintenance.usage_epoch,
                    (unsigned int)btn_up,
                   (unsigned int)btn_down,
                   (unsigned int)btn_lock,
                   (unsigned int)estop,
                   (unsigned int)upper_limit,
                   (unsigned int)lower_limit,
                   (unsigned int)refill,
                   (unsigned int)photo,
                   (unsigned int)out_motor,
                   (unsigned int)out_drop,
                   (unsigned int)out_air,
                   alarm_str,
                   (unsigned int)alarm_code,
                   (unsigned int)upper_limit,
                   (unsigned int)lower_limit,
                   (unsigned int)estop,
                   (unsigned int)photo,
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
                   dtu_state,
                   (int)csq);

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

    /* 璇诲彇鎵€鏈夎緭鍏ョ姸鎬?*/
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

    /* 璇诲彇鎵€鏈夎緭鍑虹姸鎬?*/
    uint8_t out_motor     = App_IO_Read_Output(IO_OUT_MOTOR);
    uint8_t out_drop      = App_IO_Read_Output(IO_OUT_DROP_VALVE);
    uint8_t out_main_air  = App_IO_Read_Output(IO_OUT_MAIN_AIR_VALVE);
    uint8_t out_main_work = App_IO_Read_Output(IO_OUT_MAIN_WORK_VALVE);
    uint8_t out_sub_air   = App_IO_Read_Output(IO_OUT_SUB_AIR_VALVE);
    uint8_t out_sub_work  = App_IO_Read_Output(IO_OUT_SUB_WORK_VALVE);

    const char *role_str = App_Product_RoleName(g_current_role);

    /* IoT 鐘舵€佸揩鐓э紙閬垮厤涓?DTU 浠诲姟骞跺彂锛?*/
    lift_iot_snapshot_t iot_snap;
    lift_iot_maintenance_snapshot_t maintenance;
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
    LiftIot_SnapshotMaintenance(&maintenance);

    const char *state_str    = LiftIot_StateName();
    const char *alarm_str    = LiftIot_AlarmName();
    uint8_t     alarm_code   = LiftIot_AlarmCode();
    uint32_t    avg_ms       = (iot_snap.run_count == 0U) ? 0U : (iot_snap.total_run_ms / iot_snap.run_count);

    len = snprintf(buf, size,
                   "{\"type\":\"telemetry\","
                   "\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"name\":\"%s\",\"model\":\"%s\",\"group\":\"%s\","
                   "\"product_type\":\"%s\","
                   "\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,"
                   "\"state\":\"%s\",\"rotary_switch\":\"%s\","
                    "\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,"
                    "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,\"maintenance_threshold\":%u,\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,\"maintenance_due\":%u,\"usage_epoch\":%lu},"
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
                   "\"alarm\":\"%s\",\"alarm_code\":%u,"
                   "\"upper\":%u,\"lower\":%u,\"estop\":%u,\"photoelectric\":%u"
                   "},"
                   "\"stats\":{"
                   "\"up\":%lu,\"down\":%lu,\"lock\":%lu,\"refill\":%lu,"
                   "\"estop\":%lu,\"photo_alarm\":%lu,"
                   "\"up_main\":%lu,\"up_sub\":%lu,"
                   "\"down_main\":%lu,\"down_sub\":%lu,"
                   "\"boot_count\":%lu,\"total_run_ms\":%lu"
                   "},"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   LIFT_IOT_DEVICE_ID,
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   LIFT_IOT_DEVICE_NAME,
                   LIFT_IOT_DEVICE_MODEL,
                   LIFT_IOT_DEVICE_GROUP,
                   App_Product_TypeName(g_product_type),
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   state_str,
                   role_str,
                   (unsigned int)iot_snap.locked,
                    (unsigned int)iot_snap.maintenance_due,
                    (unsigned int)iot_snap.admin_mode,
                    (unsigned long)maintenance.total_lift_count,
                    (unsigned long)maintenance.maintenance_lift_count,
                    (unsigned int)W25Q_MAINTENANCE_THRESHOLD,
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
                   (unsigned int)upper_limit,
                   (unsigned int)lower_limit,
                   (unsigned int)estop,
                   (unsigned int)photo,
                   (unsigned long)g_stats.up_count,
                   (unsigned long)g_stats.down_count,
                   (unsigned long)g_stats.lock_count,
                   (unsigned long)g_stats.refill_count,
                   (unsigned long)g_stats.estop_count,
                   (unsigned long)g_stats.photo_alarm_count,
                   (unsigned long)g_stats.up_count_main,
                   (unsigned long)g_stats.up_count_sub,
                   (unsigned long)g_stats.down_count_main,
                   (unsigned long)g_stats.down_count_sub,
                   (unsigned long)g_stats.boot_count,
                   (unsigned long)g_stats.total_run_ms,
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
    lift_iot_maintenance_snapshot_t maintenance;
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
    LiftIot_SnapshotMaintenance(&maintenance);

    const char *alarm_str  = LiftIot_AlarmName();
    uint8_t     alarm_code = LiftIot_AlarmCode();
    const char *state_str  = LiftIot_StateName();
    uint32_t    avg_ms     = (iot_snap.run_count == 0U) ? 0U : (iot_snap.total_run_ms / iot_snap.run_count);

    len = snprintf(buf, size,
                   "{\"type\":\"status\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"name\":\"%s\",\"model\":\"%s\",\"group\":\"%s\","
                   "\"product_type\":\"%s\","
                   "\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,\"event\":\"%s\",\"state\":\"%s\","
                    "\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,"
                    "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,\"maintenance_threshold\":%u,\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,\"maintenance_due\":%u,\"usage_epoch\":%lu},"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%d},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   LIFT_IOT_DEVICE_ID,
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   LIFT_IOT_DEVICE_NAME,
                   LIFT_IOT_DEVICE_MODEL,
                   LIFT_IOT_DEVICE_GROUP,
                   App_Product_TypeName(g_product_type),
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   event,
                   state_str,
                    (unsigned int)iot_snap.locked,
                    (unsigned int)iot_snap.maintenance_due,
                    (unsigned int)iot_snap.admin_mode,
                    (unsigned long)maintenance.total_lift_count,
                    (unsigned long)maintenance.maintenance_lift_count,
                    (unsigned int)W25Q_MAINTENANCE_THRESHOLD,
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
    lift_iot_maintenance_snapshot_t maintenance;
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
    LiftIot_SnapshotMaintenance(&maintenance);

    const char *alarm_str  = LiftIot_AlarmName();
    uint8_t     alarm_code = LiftIot_AlarmCode();
    const char *state_str  = LiftIot_StateName();
    uint32_t    avg_ms     = (iot_snap.run_count == 0U) ? 0U : (iot_snap.total_run_ms / iot_snap.run_count);

    len = snprintf(buf, size,
                   "{\"type\":\"status\",\"device\":\"%s\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"name\":\"%s\",\"model\":\"%s\",\"group\":\"%s\","
                   "\"product_type\":\"%s\","
                   "\"seq\":%lu,\"tick\":%lu,\"uptime_ms\":%lu,\"event\":\"%s\","
                   "\"cmd\":\"%s\",\"msg_id\":\"%s\",\"result\":\"%s\","
                    "\"state\":\"%s\",\"locked\":%u,\"maintenance_due\":%u,\"admin_mode\":%u,"
                    "\"maintenance\":{\"total_lift_count\":%lu,\"maintenance_lift_count\":%lu,\"maintenance_threshold\":%u,\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,\"maintenance_due\":%u,\"usage_epoch\":%lu},"
                   "\"runtime\":{\"total_ms\":%lu,\"current_ms\":%lu,\"run_count\":%lu,\"avg_ms\":%lu},"
                   "\"safety\":{\"alarm\":\"%s\",\"alarm_code\":%d},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   LIFT_IOT_DEVICE_ID,
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   LIFT_IOT_DEVICE_NAME,
                   LIFT_IOT_DEVICE_MODEL,
                   LIFT_IOT_DEVICE_GROUP,
                   App_Product_TypeName(g_product_type),
                   (unsigned long)seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   event,
                   cmd,
                   msg_id,
                   result,
                   state_str,
                   (unsigned int)iot_snap.locked,
                    (unsigned int)iot_snap.maintenance_due,
                    (unsigned int)iot_snap.admin_mode,
                    (unsigned long)maintenance.total_lift_count,
                    (unsigned long)maintenance.maintenance_lift_count,
                    (unsigned int)W25Q_MAINTENANCE_THRESHOLD,
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

    /* 澶у壀鏃犵紪鐮佸櫒锛岃繑鍥?stub JSON */
    len = snprintf(buf, size,
                   "{\"type\":\"motion\",\"uid\":\"%s\",\"chip_uid\":\"%s\","
                   "\"product_type\":\"thin_scissor\",\"seq\":%lu,\"tick\":%lu,"
                   "\"state\":\"%s\"}",
                   LiftIot_ChipUidString(),
                   LiftIot_ChipUidString(),
                   (unsigned long)seq,
                   (unsigned long)HAL_GetTick(),
                   LiftIot_StateName());

    if ((len < 0) || (len >= (int)size))
    {
        return LIFT_IOT_BUFFER_SMALL;
    }

    return LIFT_IOT_OK;
}

/* ================= 6. 鎿嶄綔鏃ュ織鎵归噺涓婁紶 JSON ================= */

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

    /* 璧峰 JSON 澶?*/
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

    /* 寰幆璇诲彇鏃ュ織锛屽崟甯ф渶澶?count 鏉★紙鍙楃紦鍐插尯澶у皬闄愬埗锛?*/
    for (i = 0; i < count && (start_index + i) < total; i++)
    {
        w25q_op_log_entry_t entry;
        if (App_OpLog_Read(start_index + i, &entry) != 0U)
        {
            break;
        }

        /* detail 杞?hex 瀛楃涓诧紙鏈€澶?8 瀛楄妭 = 16 hex 瀛楃锛?*/
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
            /* 缂撳啿鍖轰笉瓒筹紝鍋滄娣诲姞 */
            break;
        }
        pos += ret;
        written++;
    }

    /* 缁撴潫 JSON */
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

/* ================= 7. 杩滅▼鍛戒护澶勭悊锛堟柊鍏ュ彛锛?================= */

lift_iot_result_t LiftIot_HandleClearAlarm(const char *account)
{
    LiftCore_ClearAlarm();
    LiftLock_LockIot();
    g_lift_iot_status.last_command_tick = HAL_GetTick();
    g_iot_event_pending = 1U;  /* 瑙﹀彂 telemetry 涓婃姤 */
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

/* ================= 8. 浜嬩欢涓婃姤閽╁瓙 ================= */

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

/* ================= 9. 蹇収鎺ュ彛 ================= */

void LiftIot_Snapshot(lift_state_snapshot_t *out_state,
                      lift_iot_snapshot_t  *out_iot)
{
    if (out_state == NULL || out_iot == NULL)
    {
        return;
    }

    /* 椤哄簭锛氬厛 state 鍚?iot锛堜笌浠ｇ爜涓叾瀹冨湴鏂硅幏鍙栭『搴忎竴鑷达紝閬垮厤娼滃湪鍙嶈浆锛?*/
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

void LiftIot_SnapshotMaintenance(lift_iot_maintenance_snapshot_t *out_maintenance)
{
    if (out_maintenance == NULL)
    {
        return;
    }

    LiftLock_LockIot();
    out_maintenance->total_lift_count = g_maintenance.total_lift_count;
    out_maintenance->maintenance_lift_count = g_maintenance.maintenance_lift_count;
    out_maintenance->maintenance_count = g_maintenance.maintenance_count;
    out_maintenance->last_maintenance_total = g_maintenance.last_maintenance_total;
    out_maintenance->maintenance_due = g_maintenance.maintenance_due;
    out_maintenance->usage_epoch = g_maintenance.usage_epoch;
    LiftLock_UnlockIot();
}

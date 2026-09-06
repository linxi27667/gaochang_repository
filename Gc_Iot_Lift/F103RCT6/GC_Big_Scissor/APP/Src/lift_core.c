#include "lift_core.h"
#include "app_io_map.h"
#include "app_w25qxx.h"
#include "app_op_log.h"
#include "app_product.h"
#include "lift_iot.h"     /* 为 NotifyEstop / NotifyPhotoAlarm 事件钩子 */
#include "lift_lock.h"    /* 互斥锁 */
#include "stm32f1xx_hal.h"
#include "main.h"

#if LIFT_CORE_DEBUG == 1
#include "elog.h"
#endif

/* ============ 外部声明：各型号 ops ============ */
extern const lift_ops_t large_scissor_ops;  /* lift_large_scissor.c */
/* 其他型号预留：
 * extern const lift_ops_t double_post_ops;
 * extern const lift_ops_t small_scissor_ops;
 * extern const lift_ops_t thin_scissor_ops;
 */

/* ============ 全局对象 ============ */
volatile lift_state_t g_lift_state = LIFT_STATE_IDLE;
const lift_ops_t *g_lift_ops = NULL;

/* 远程锁定标志（IoT 命令设置） */
volatile uint8_t s_remote_locked = 0;
static uint8_t s_photo_alarm_requires_remote_clear = 0U;
static uint8_t s_motion_rearm_required = 1U;

/* ============ 状态名 ============ */
const char *LiftCore_StateName(lift_state_t state)
{
    switch (state) {
        case LIFT_STATE_IDLE:        return "idle";
        case LIFT_STATE_RISING:      return "rising";
        case LIFT_STATE_DROPPING:    return "dropping";
        case LIFT_STATE_LOCKED:      return "locked";
        case LIFT_STATE_REFILLING:   return "refilling";
        case LIFT_STATE_ESTOP:       return "estop";
        case LIFT_STATE_PHOTO_ALARM: return "photo_alarm";
        default:                     return "unknown";
    }
}

static uint8_t LiftCore_MotionButtonsReleased(void)
{
    return (uint8_t)((App_IO_Read(IO_IN_UP_BUTTON) == 0U) &&
                     (App_IO_Read(IO_IN_DOWN_BUTTON) == 0U) &&
                     (App_IO_Read(IO_IN_LOCK_BUTTON) == 0U) &&
                     (App_IO_Read(IO_IN_REFILL_BUTTON) == 0U));
}

static uint8_t LiftCore_IsMotionState(lift_state_t state)
{
    return (uint8_t)((state == LIFT_STATE_RISING) ||
                     (state == LIFT_STATE_DROPPING) ||
                     (state == LIFT_STATE_LOCKED) ||
                     (state == LIFT_STATE_REFILLING));
}

void LiftCore_RequireMotionRearm(void)
{
    LiftLock_LockState();
    s_motion_rearm_required = 1U;
    LiftLock_UnlockState();
}

static uint8_t LiftCore_MotionRearmPending(void)
{
    uint8_t buttons_released = LiftCore_MotionButtonsReleased();

    LiftLock_LockState();
    if ((s_motion_rearm_required != 0U) &&
        (buttons_released != 0U) &&
        (s_remote_locked == 0U) &&
        (g_lift_state != LIFT_STATE_ESTOP) &&
        (g_lift_state != LIFT_STATE_PHOTO_ALARM)) {
        s_motion_rearm_required = 0U;
    }
    uint8_t pending = s_motion_rearm_required;
    LiftLock_UnlockState();

    if (pending != 0U) {
        App_IO_EmergencyOff();
    }
    return pending;
}

/* ============ 初始化 ============ */
void LiftCore_Init(void)
{
    /* 根据产品类型选择 ops */
    switch (g_product_type) {
        case PRODUCT_TYPE_LARGE_SCISSOR:
            g_lift_ops = &large_scissor_ops;
            break;
        /* 其他型号预留 */
        default:
            #if LIFT_CORE_DEBUG == 1
            elog_e("CORE", "[CORE] No ops for product_type=%d, fallback to large_scissor",
                   g_product_type);
            #endif
            g_lift_ops = &large_scissor_ops;
            break;
    }

    g_lift_state = LIFT_STATE_IDLE;
    s_remote_locked = 0;
    s_photo_alarm_requires_remote_clear = 0U;
    s_motion_rearm_required = 1U;

    if (g_lift_ops && g_lift_ops->init) {
        g_lift_ops->init();
    }

    /* 记录开机日志 */
    App_OpLog_Record(OP_POWER_ON, OP_RESULT_OK, 0, NULL, 0);

    #if LIFT_CORE_DEBUG == 1
    elog_i("CORE", "[CORE] Init done, product=%s state=%s",
           App_Product_TypeName(g_product_type), LiftCore_StateName(g_lift_state));
    #endif
}

/* ============ 周期调用 ============ */
void LiftCore_Poll(void)
{
    if (g_lift_ops == NULL) return;

    /* 1. 急停检测（最高优先级） */
    uint8_t estop = App_IO_Read(IO_IN_ESTOP);
    if (estop) {
        LiftLock_LockState();
        uint8_t need_handle = (g_lift_state != LIFT_STATE_ESTOP) ? 1U : 0U;
        if (need_handle) {
            g_lift_state = LIFT_STATE_ESTOP;
        }
        s_motion_rearm_required = 1U;
        LiftLock_UnlockState();

        if (need_handle) {
            App_IO_All_Off();
            App_W25Qxx_Stats_Inc_Estop();
            if (g_lift_ops->on_estop) g_lift_ops->on_estop();
            App_OpLog_Record(OP_ESTOP, OP_RESULT_INTERRUPTED, 0, NULL, 0);
            LiftIot_NotifyEstop();
            App_IO_LogSnapshot("estop_trigger");
            #if LIFT_CORE_DEBUG == 1
            elog_w("CORE", "[CORE] E-STOP triggered");
            #endif
        }
        return;
    } else {
        LiftLock_LockState();
        uint8_t in_estop = (g_lift_state == LIFT_STATE_ESTOP) ? 1U : 0U;
        if (in_estop) {
            g_lift_state = LIFT_STATE_IDLE;
        }
        LiftLock_UnlockState();

        if (in_estop) {
            App_IO_LogSnapshot("estop_release");
            #if LIFT_CORE_DEBUG == 1
            elog_i("CORE", "[CORE] E-STOP released, back to IDLE");
            #endif
        }
    }

    /* 2. 光电报警检测（第二优先级）
     * 大剪主机下限位保持触发期间持续屏蔽光电，限位释放后自动恢复。 */
    uint8_t skip_photo = 0U;
    if (g_product_type == PRODUCT_TYPE_LARGE_SCISSOR) {
        skip_photo = App_IO_Read(IO_IN_LOWER_LIMIT);
    }

    /* 主机下限位触发：若当前在 PHOTO_ALARM 状态，自动解除报警回 IDLE */
    if (skip_photo) {
        LiftLock_LockState();
        uint8_t in_photo = (g_lift_state == LIFT_STATE_PHOTO_ALARM) ? 1U : 0U;
        if (in_photo && (s_photo_alarm_requires_remote_clear == 0U)) {
            g_lift_state = LIFT_STATE_IDLE;
        }
        LiftLock_UnlockState();

        if (in_photo && (s_photo_alarm_requires_remote_clear == 0U)) {
            if (g_lift_ops && g_lift_ops->on_clear_alarm) {
                g_lift_ops->on_clear_alarm();
            }
            App_OpLog_Record(OP_REMOTE_CLEAR_ALARM, OP_RESULT_OK, 0, NULL, 0);
            #if LIFT_CORE_DEBUG == 1
            elog_i("CORE", "[CORE] Photo alarm auto-cleared by main lower limit");
            #endif
        }
    }

    uint8_t photo = App_IO_Read(IO_IN_PHOTOELECTRIC);
    if (photo && !skip_photo) {
        LiftLock_LockState();
        uint8_t need_handle = (g_lift_state != LIFT_STATE_PHOTO_ALARM) ? 1U : 0U;
        if (need_handle) {
            s_photo_alarm_requires_remote_clear = LiftCore_IsMotionState(g_lift_state);
            g_lift_state = LIFT_STATE_PHOTO_ALARM;
            s_motion_rearm_required = 1U;
        }
        LiftLock_UnlockState();

        if (need_handle) {
            App_IO_All_Off();
            App_W25Qxx_Stats_Inc_PhotoAlarm();
            if (g_lift_ops->on_photoelectric_blocked) g_lift_ops->on_photoelectric_blocked();
            App_OpLog_Record(OP_PHOTO_ALARM, OP_RESULT_INTERRUPTED, 0, NULL, 0);
            LiftIot_NotifyPhotoAlarm();
            App_IO_LogSnapshot("photo_alarm");
            #if LIFT_CORE_DEBUG == 1
            elog_w("CORE", "[CORE] Photoelectric blocked, alarm triggered");
            #endif
        }
        return;
    }

    /* 3. 光电报警状态需远程解除（主机下限位触发时已自动解除） */
    LiftLock_LockState();
    uint8_t in_photo_alarm = (g_lift_state == LIFT_STATE_PHOTO_ALARM) ? 1U : 0U;
    uint8_t locked         = s_remote_locked;
    LiftLock_UnlockState();
    if (in_photo_alarm) {
        if ((photo == 0U) &&
            (s_photo_alarm_requires_remote_clear == 0U) &&
            LiftCore_MotionButtonsReleased()) {
            LiftLock_LockState();
            if (g_lift_state == LIFT_STATE_PHOTO_ALARM) {
                g_lift_state = LIFT_STATE_IDLE;
            }
            LiftLock_UnlockState();

            if (g_lift_ops && g_lift_ops->on_clear_alarm) {
                g_lift_ops->on_clear_alarm();
            }
            App_OpLog_Record(OP_REMOTE_CLEAR_ALARM, OP_RESULT_OK, 0, NULL, 0);
            #if LIFT_CORE_DEBUG == 1
            elog_i("CORE", "[CORE] Photo alarm auto-cleared by photo release");
            #endif
        }
        return;
    }

    /* 4. 远程锁定状态：不响应按钮 */
    if (locked) {
        App_IO_EmergencyOff();
        return;
    }

    /* 5. 安全恢复后必须先释放全部动作按钮。 */
    if (LiftCore_MotionRearmPending() != 0U) {
        return;
    }

    /* 6. 分发到产品 ops->poll() */
    if (g_lift_ops->poll) {
        g_lift_ops->poll();
    }

    /* 远程锁定可能由其他任务并发设置，产品 poll 返回后再次确保输出断开。 */
    LiftLock_LockState();
    locked = s_remote_locked;
    LiftLock_UnlockState();
    if (locked != 0U) {
        App_IO_All_Off();
        if (g_lift_ops->on_remote_lock) {
            g_lift_ops->on_remote_lock();
        }
    }
}

/* ============ 远程解除报警 ============ */
void LiftCore_ClearAlarm(void)
{
    LiftLock_LockState();
    uint8_t in_alarm = (g_lift_state == LIFT_STATE_PHOTO_ALARM) ? 1U : 0U;
    if (in_alarm) {
        g_lift_state = LIFT_STATE_IDLE;
        s_photo_alarm_requires_remote_clear = 0U;
        s_motion_rearm_required = 1U;
    }
    LiftLock_UnlockState();

    if (in_alarm) {
        if (g_lift_ops && g_lift_ops->on_clear_alarm) {
            g_lift_ops->on_clear_alarm();
        }
        App_OpLog_Record(OP_REMOTE_CLEAR_ALARM, OP_RESULT_OK, 0, NULL, 0);
        #if LIFT_CORE_DEBUG == 1
        elog_i("CORE", "[CORE] Photo alarm cleared by remote");
        #endif
    }
}

/* ============ 远程锁定/解锁 ============ */
void LiftCore_SetRemoteLock(uint8_t locked)
{
    LiftLock_LockState();
    s_remote_locked = locked ? 1 : 0;
    if (s_remote_locked) {
        if ((g_lift_state != LIFT_STATE_ESTOP) &&
            (g_lift_state != LIFT_STATE_PHOTO_ALARM)) {
            g_lift_state = LIFT_STATE_IDLE;
        }
        s_motion_rearm_required = 1U;
    }
    LiftLock_UnlockState();

    if (locked) {
        App_IO_All_Off();
        if (g_lift_ops && g_lift_ops->on_remote_lock) {
            g_lift_ops->on_remote_lock();
        }
        App_OpLog_Record(OP_REMOTE_LOCK, OP_RESULT_OK, 0, NULL, 0);
    } else {
        App_OpLog_Record(OP_REMOTE_UNLOCK, OP_RESULT_OK, 0, NULL, 0);
    }
    #if LIFT_CORE_DEBUG == 1
    elog_i("CORE", "[CORE] Remote lock=%u", locked);
    #endif
}

/* ============ 旧版远程锁定/解锁已被替换为持锁版本（见上方） ============ */
#if 0
void LiftCore_SetRemoteLock_Old(uint8_t locked)
{
    s_remote_locked = locked ? 1 : 0;
    if (s_remote_locked) {
        /* 锁定时断开所有输出 */
        App_IO_All_Off();
        g_lift_state = LIFT_STATE_IDLE;
        App_OpLog_Record(OP_REMOTE_LOCK, OP_RESULT_OK, 0, NULL, 0);
    } else {
        App_OpLog_Record(OP_REMOTE_UNLOCK, OP_RESULT_OK, 0, NULL, 0);
    }
    #if LIFT_CORE_DEBUG == 1
    elog_i("CORE", "[CORE] Remote lock=%u", s_remote_locked);
    #endif
}
#endif

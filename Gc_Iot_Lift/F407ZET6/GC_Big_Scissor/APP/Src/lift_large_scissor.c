#include "lift_core.h"
#include "app_io_map.h"
#include "app_w25qxx.h"
#include "app_op_log.h"
#include "app_product.h"
#include "stm32f4xx_hal.h"
#include "main.h"

#if LARGE_SCISSOR_DEBUG == 1
#include "elog.h"
#endif

/* ============ 大剪内部子状态 ============ */
typedef enum {
    LS_IDLE = 0,
    LS_UP_WAITING_AIR,    /* 上升：电机+工作阀已开，等待气阀延时 */
    LS_UP_RUNNING,        /* 上升：气阀已开，运行中 */
    LS_DOWN_PHASE_1,      /* 下降阶段1：电机+工作阀，等待气阀延时 (200ms) */
    LS_DOWN_PHASE_2,      /* 下降阶段2：气阀已开，等待电机保持时间 (3000/1500ms) */
    LS_DOWN_DROPPING,     /* 下降阶段3：电机断开，下降阀打开 */
    LS_LOCKED,            /* 锁定保持：工作阀+下降阀开 */
    LS_REFILLING          /* 补油中：电机+工作阀(+气阀)开 */
} ls_sub_state_t;

/* ============ 模块内部状态 ============ */
static ls_sub_state_t s_ls_state = LS_IDLE;
static uint32_t       s_action_start_tick = 0;
static uint32_t       s_phase_start_tick = 0;

/* 按钮边沿检测（上一周期状态） */
static uint8_t s_prev_up     = 0;
static uint8_t s_prev_down   = 0;
static uint8_t s_prev_lock   = 0;

/* ============ 辅助函数 ============ */

static io_out_id_t role_work_valve(lift_role_t role)
{
    return (role == LIFT_ROLE_MAIN) ? IO_OUT_MAIN_WORK_VALVE : IO_OUT_SUB_WORK_VALVE;
}

static io_out_id_t role_air_valve(lift_role_t role)
{
    return (role == LIFT_ROLE_MAIN) ? IO_OUT_MAIN_AIR_VALVE : IO_OUT_SUB_AIR_VALVE;
}

static uint16_t role_motor_hold_ms(lift_role_t role)
{
    return (role == LIFT_ROLE_MAIN) ? g_config.motor_hold_ms : g_config.sub_motor_hold_ms;
}

/* 按角色选择上限位：主机用 PE5，子机用 PE7 */
static io_in_id_t role_upper_limit(lift_role_t role)
{
    return (role == LIFT_ROLE_MAIN) ? IO_IN_UPPER_LIMIT : IO_IN_SUB_UPPER_LIMIT;
}

/* Force direct dropping: motor off, work/air/drop valves on. */
static void ls_force_down(lift_role_t role)
{
    App_IO_Write(IO_OUT_MOTOR, 0);
    App_IO_Write(role_work_valve(role), 1);
    App_IO_Write(role_air_valve(role), 1);
    App_IO_Write(IO_OUT_DROP_VALVE, 1);
    s_ls_state = LS_DOWN_DROPPING;
    g_lift_state = LIFT_STATE_DROPPING;
}

static void ls_stop_all_to_idle(void)
{
    App_IO_All_Off();
    s_ls_state = LS_IDLE;
    s_phase_start_tick = 0;
    g_lift_state = LIFT_STATE_IDLE;
}

/* ============ 初始化 ============ */
static void large_scissor_init(void)
{
    s_ls_state = LS_IDLE;
    s_action_start_tick = 0;
    s_phase_start_tick = 0;
    s_prev_up = s_prev_down = s_prev_lock = 0;
    App_IO_All_Off();

    /* 读取旋转开关初始状态 */
    g_current_role = App_IO_Read(IO_IN_ROTARY_SWITCH) ? LIFT_ROLE_MAIN : LIFT_ROLE_SUB;

#if LARGE_SCISSOR_DEBUG == 1
    elog_i("LSCS", "[LSCS] Init done, role=%s", App_Product_RoleName(g_current_role));
#endif
}

/* ============ 上升流程 ============ */
static void ls_start_up(lift_role_t role)
{
    if (App_IO_Read(role_upper_limit(role)) != 0U) {
#if LARGE_SCISSOR_DEBUG == 1
        elog_w("LSCS", "[LSCS] UP blocked by upper limit role=%s",
               App_Product_RoleName(role));
#endif
        return;
    }

    App_IO_Write(IO_OUT_MOTOR, 1);
    App_IO_Write(role_work_valve(role), 1);
    s_action_start_tick = HAL_GetTick();
    s_phase_start_tick = s_action_start_tick;
    s_ls_state = LS_UP_WAITING_AIR;
    g_lift_state = LIFT_STATE_RISING;

    uint8_t detail = (uint8_t)role;
    App_OpLog_Record(OP_UP_START, OP_RESULT_OK, 0, &detail, 1);

#if LARGE_SCISSOR_DEBUG == 1
    elog_i("LSCS", "[LSCS] UP start role=%s", App_Product_RoleName(role));
#endif
}

static void ls_poll_up(lift_role_t role)
{
    uint32_t now = HAL_GetTick();

    /* 气阀延时 */
    if (s_ls_state == LS_UP_WAITING_AIR) {
        if ((now - s_phase_start_tick) >= g_config.motor_to_valve_delay_ms) {
            App_IO_Write(role_air_valve(role), 1);
            s_ls_state = LS_UP_RUNNING;
#if LARGE_SCISSOR_DEBUG == 1
            elog_i("LSCS", "[LSCS] UP air valve ON role=%s elapsed=%lu target=%u",
                   App_Product_RoleName(role),
                   (unsigned long)(now - s_phase_start_tick),
                   (unsigned int)g_config.motor_to_valve_delay_ms);
#endif
        }
    }

    /* 退出条件：上限位触发（按角色选主机/子机上限位）或 按钮释放 */
    uint8_t upper_limit = App_IO_Read(role_upper_limit(role));
    uint8_t up_pressed  = App_IO_Read(IO_IN_UP_BUTTON);

    if (upper_limit) {
        /* 上限位触发：全停 */
        uint16_t dur = (uint16_t)(now - s_action_start_tick);
        ls_stop_all_to_idle();
        uint8_t detail = (uint8_t)role;
        App_OpLog_Record(OP_UP_STOP_LIMIT, OP_RESULT_OK, dur, &detail, 1);
#if LARGE_SCISSOR_DEBUG == 1
        elog_i("LSCS", "[LSCS] UP stop by limit, dur=%u", dur);
#endif
    } else if (!up_pressed) {
        /* 按钮释放：全停 */
        uint16_t dur = (uint16_t)(now - s_action_start_tick);
        ls_stop_all_to_idle();
        uint8_t detail = (uint8_t)role;
        App_OpLog_Record(OP_UP_STOP_RELEASE, OP_RESULT_OK, dur, &detail, 1);
#if LARGE_SCISSOR_DEBUG == 1
        elog_i("LSCS", "[LSCS] UP stop by release, dur=%u", dur);
#endif
    }
}

/* ============ 下降流程 ============ */
static void ls_start_down(lift_role_t role)
{
    s_action_start_tick = HAL_GetTick();
    s_phase_start_tick = s_action_start_tick;
    g_lift_state = LIFT_STATE_DROPPING;

    uint8_t detail = (uint8_t)role;
    App_OpLog_Record(OP_DOWN_START, OP_RESULT_OK, 0, &detail, 1);
    App_W25Qxx_Stats_Inc_Down(role);

    if ((App_IO_Read(role_upper_limit(role)) != 0U) ||
        (App_IO_Read(IO_IN_LOCK_BUTTON) != 0U)) {
        ls_force_down(role);
#if LARGE_SCISSOR_DEBUG == 1
        elog_i("LSCS", "[LSCS] DOWN force start role=%s upper=%u lock=%u",
               App_Product_RoleName(role),
               (unsigned int)App_IO_Read(role_upper_limit(role)),
               (unsigned int)App_IO_Read(IO_IN_LOCK_BUTTON));
#endif
        return;
    }

    App_IO_Write(IO_OUT_MOTOR, 1);
    App_IO_Write(role_work_valve(role), 1);
    s_ls_state = LS_DOWN_PHASE_1;

#if LARGE_SCISSOR_DEBUG == 1
    elog_i("LSCS", "[LSCS] DOWN start role=%s air_delay=%u hold_after_air=%u",
           App_Product_RoleName(role),
           (unsigned int)g_config.motor_to_valve_delay_ms,
           (unsigned int)role_motor_hold_ms(role));
#endif
}

static void ls_poll_down(lift_role_t role)
{
    uint32_t now = HAL_GetTick();
    uint16_t hold_ms = role_motor_hold_ms(role);

    /* 阶段1→2：气阀延时 (200ms) */
    if (s_ls_state == LS_DOWN_PHASE_1) {
        if ((now - s_phase_start_tick) >= g_config.motor_to_valve_delay_ms) {
            App_IO_Write(role_air_valve(role), 1);
            s_phase_start_tick = now;
            s_ls_state = LS_DOWN_PHASE_2;
#if LARGE_SCISSOR_DEBUG == 1
            elog_i("LSCS", "[LSCS] DOWN air valve ON role=%s elapsed=%lu target=%u",
                   App_Product_RoleName(role),
                   (unsigned long)(now - s_action_start_tick),
                   (unsigned int)g_config.motor_to_valve_delay_ms);
#endif
        }
    }
    /* 阶段2→3：电机保持时间到，电机断开，下降阀打开 */
    else if (s_ls_state == LS_DOWN_PHASE_2) {
        if ((now - s_phase_start_tick) >= hold_ms) {
            App_IO_Write(role_work_valve(role), 1);
            App_IO_Write(role_air_valve(role), 1);
            App_IO_Write(IO_OUT_MOTOR, 0);
            App_IO_Write(IO_OUT_DROP_VALVE, 1);
            s_ls_state = LS_DOWN_DROPPING;
#if LARGE_SCISSOR_DEBUG == 1
            elog_i("LSCS", "[LSCS] DOWN motor OFF, drop valve ON role=%s hold_elapsed=%lu hold_target=%u total_elapsed=%lu",
                   App_Product_RoleName(role),
                   (unsigned long)(now - s_phase_start_tick),
                   (unsigned int)hold_ms,
                   (unsigned long)(now - s_action_start_tick));
#endif
        }
    }

    /* 强制下降条件：上限位触发（按角色选主机/子机上限位）或 锁定按钮按下
     * 电机断开，工作阀+气阀+下降阀立即打开
     */
    /* Upper limit or lock button cancels pre-lift timing and forces direct dropping. */
    uint8_t upper_limit = App_IO_Read(role_upper_limit(role));
    uint8_t lock_pressed= App_IO_Read(IO_IN_LOCK_BUTTON);
    uint8_t down_pressed= App_IO_Read(IO_IN_DOWN_BUTTON);

    if (upper_limit || lock_pressed) {
        uint8_t force_transition = (s_ls_state != LS_DOWN_DROPPING) ? 1U : 0U;
        /* 强制下降：电机断开，所有阀打开 */
        ls_force_down(role);
#if LARGE_SCISSOR_DEBUG == 1
        if (force_transition) {
            elog_i("LSCS", "[LSCS] DOWN forced role=%s upper=%u lock=%u",
                   App_Product_RoleName(role),
                   (unsigned int)upper_limit,
                   (unsigned int)lock_pressed);
        }
#endif

        /* 不退出，保持下降状态直到按钮释放 */
    }

    /* 按钮释放：全停，回 IDLE */
    if (!down_pressed) {
        uint16_t dur = (uint16_t)(now - s_action_start_tick);
        ls_stop_all_to_idle();
        uint8_t detail = (uint8_t)role;
        App_OpLog_Record(OP_DOWN_STOP_RELEASE, OP_RESULT_OK, dur, &detail, 1);
#if LARGE_SCISSOR_DEBUG == 1
        elog_i("LSCS", "[LSCS] DOWN stop by release, dur=%u", dur);
#endif
    }
}

/* ============ 锁定流程 ============ */
static void ls_start_lock(lift_role_t role)
{
    App_IO_Write(role_work_valve(role), 1);
    App_IO_Write(IO_OUT_DROP_VALVE, 1);
    s_ls_state = LS_LOCKED;
    s_action_start_tick = HAL_GetTick();
    s_phase_start_tick = s_action_start_tick;
    g_lift_state = LIFT_STATE_LOCKED;

    uint8_t detail = (uint8_t)role;
    App_OpLog_Record(OP_LOCK_START, OP_RESULT_OK, 0, &detail, 1);
    App_W25Qxx_Stats_Inc_Lock();

#if LARGE_SCISSOR_DEBUG == 1
    elog_i("LSCS", "[LSCS] LOCK start role=%s", App_Product_RoleName(role));
#endif
}

static void ls_poll_lock(lift_role_t role)
{
    (void)role;
    uint32_t now = HAL_GetTick();
    uint8_t lock_pressed = App_IO_Read(IO_IN_LOCK_BUTTON);

    if (!lock_pressed) {
        uint16_t dur = (uint16_t)(now - s_action_start_tick);
        ls_stop_all_to_idle();
        App_OpLog_Record(OP_LOCK_STOP, OP_RESULT_OK, dur, NULL, 0);
#if LARGE_SCISSOR_DEBUG == 1
        elog_i("LSCS", "[LSCS] LOCK stop, dur=%u", dur);
#endif
    }
}

/* ============ 补油流程（上升+补油同时按下） ============ */
static void ls_start_refill(lift_role_t role)
{
    App_IO_Write(IO_OUT_MOTOR, 1);
    App_IO_Write(role_work_valve(role), 1);
    /* 主机补油开气阀，子机补油不开气阀（按用户描述） */
    s_ls_state = LS_REFILLING;
    s_action_start_tick = HAL_GetTick();
    s_phase_start_tick = s_action_start_tick;
    g_lift_state = LIFT_STATE_REFILLING;

    uint8_t detail = (uint8_t)role;
    App_OpLog_Record(OP_REFILL_START, OP_RESULT_OK, 0, &detail, 1);
    App_W25Qxx_Stats_Inc_Refill();

#if LARGE_SCISSOR_DEBUG == 1
    elog_i("LSCS", "[LSCS] REFILL start role=%s", App_Product_RoleName(role));
#endif
}

static void ls_poll_refill(lift_role_t role)
{
    uint32_t now = HAL_GetTick();
    uint8_t up_pressed = App_IO_Read(IO_IN_UP_BUTTON);
    uint8_t refill_pressed = App_IO_Read(IO_IN_REFILL_BUTTON);

    if ((now - s_phase_start_tick) >= g_config.motor_to_valve_delay_ms) {
        App_IO_Write(role_air_valve(role), 1);
    }

    /* 任一按钮释放：全停 */
    if (!up_pressed || !refill_pressed) {
        uint16_t dur = (uint16_t)(now - s_action_start_tick);
        ls_stop_all_to_idle();
        App_OpLog_Record(OP_REFILL_STOP, OP_RESULT_OK, dur, NULL, 0);
#if LARGE_SCISSOR_DEBUG == 1
        elog_i("LSCS", "[LSCS] REFILL stop, dur=%u", dur);
#endif
    }
}

/* ============ 急停/光电/远程解除 回调 ============ */
static void large_scissor_abort_motion(void)
{
    App_IO_All_Off();
    s_ls_state = LS_IDLE;
    s_action_start_tick = 0;
    s_phase_start_tick = 0;
}

static void large_scissor_on_estop(void)
{
    large_scissor_abort_motion();
}

static void large_scissor_on_photoelectric_blocked(void)
{
    large_scissor_abort_motion();
}

static void large_scissor_on_clear_alarm(void)
{
    large_scissor_abort_motion();
}

static void large_scissor_on_remote_lock(void)
{
    large_scissor_abort_motion();
}

/* ============ 主 poll 函数 ============ */
static void large_scissor_poll(void)
{
    /* 1. 读取旋转开关，检测角色变化 */
    lift_role_t new_role = App_IO_Read(IO_IN_ROTARY_SWITCH) ? LIFT_ROLE_MAIN : LIFT_ROLE_SUB;
    if (new_role != g_current_role) {
        /* 角色切换：立即停止所有输出 */
        App_IO_All_Off();
        uint8_t detail = (uint8_t)new_role;
        App_OpLog_Record(OP_ROTARY_SWITCH, OP_RESULT_OK, 0, &detail, 1);
        s_ls_state = LS_IDLE;
        s_phase_start_tick = 0;
        g_lift_state = LIFT_STATE_IDLE;
        g_current_role = new_role;
        LiftCore_RequireMotionRearm();
#if LARGE_SCISSOR_DEBUG == 1
        elog_i("LSCS", "[LSCS] Role switched to %s", App_Product_RoleName(new_role));
#endif
        return;  /* 切换周期不处理按钮 */
    }

    /* 2. 读取按钮当前状态 */
    uint8_t up_pressed     = App_IO_Read(IO_IN_UP_BUTTON);
    uint8_t down_pressed   = App_IO_Read(IO_IN_DOWN_BUTTON);
    uint8_t lock_pressed   = App_IO_Read(IO_IN_LOCK_BUTTON);
    uint8_t refill_pressed = App_IO_Read(IO_IN_REFILL_BUTTON);

    /* 3. 根据当前子状态分发处理 */
    switch (s_ls_state) {
        case LS_IDLE:
            /* 检测按钮按下边沿（从 IDLE 启动新动作） */
            /* 优先级：补油(上+补) > 上升 > 下降 > 锁定 */
            if (up_pressed && refill_pressed) {
                ls_start_refill(g_current_role);
            } else if (up_pressed && !s_prev_up) {
                ls_start_up(g_current_role);
            } else if (down_pressed && !s_prev_down) {
                ls_start_down(g_current_role);
            } else if (lock_pressed && !s_prev_lock) {
                ls_start_lock(g_current_role);
            }
            break;

        case LS_UP_WAITING_AIR:
        case LS_UP_RUNNING:
            ls_poll_up(g_current_role);
            break;

        case LS_DOWN_PHASE_1:
        case LS_DOWN_PHASE_2:
        case LS_DOWN_DROPPING:
            ls_poll_down(g_current_role);
            break;

        case LS_LOCKED:
            ls_poll_lock(g_current_role);
            break;

        case LS_REFILLING:
            ls_poll_refill(g_current_role);
            break;

        default:
            s_ls_state = LS_IDLE;
            break;
    }

    /* 4. 更新按钮历史状态（用于边沿检测） */
    s_prev_up     = up_pressed;
    s_prev_down   = down_pressed;
    s_prev_lock   = lock_pressed;
}

/* ============ ops 实例 ============ */
const lift_ops_t large_scissor_ops = {
    .init                    = large_scissor_init,
    .on_up_pressed           = NULL,  /* 在 poll 中处理 */
    .on_down_pressed         = NULL,
    .on_lock_pressed         = NULL,
    .on_refill_pressed       = NULL,
    .on_estop                = large_scissor_on_estop,
    .on_photoelectric_blocked= large_scissor_on_photoelectric_blocked,
    .on_clear_alarm          = large_scissor_on_clear_alarm,
    .on_remote_lock          = large_scissor_on_remote_lock,
    .poll                    = large_scissor_poll,
};

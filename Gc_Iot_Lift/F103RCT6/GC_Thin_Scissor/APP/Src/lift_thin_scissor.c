#include "lift_core.h"
#include "app_io_map.h"
#include "app_op_log.h"
#include "app_w25qxx.h"
#include "stm32f1xx_hal.h"
#include "main.h"
#include "elog.h"

#ifndef THIN_SCISSOR_DEBUG
#define THIN_SCISSOR_DEBUG 1
#endif

#if THIN_SCISSOR_DEBUG == 1
#define THIN_LOG_I(...)  elog_i(__VA_ARGS__)
#define THIN_LOG_W(...)  elog_w(__VA_ARGS__)
#else
#define THIN_LOG_I(...)
#define THIN_LOG_W(...)
#endif

typedef enum {
    TS_IDLE = 0,
    TS_RISING,
    TS_DOWN_PREPARE,
    TS_DOWN_HOLD_MOTOR,
    TS_DROPPING,
    TS_LOCKING,
    TS_REFILLING
} ts_sub_state_t;

static ts_sub_state_t s_ts_state = TS_IDLE;
static uint32_t s_action_start_tick;
static uint8_t s_prev_up;
static uint8_t s_prev_down;
static uint8_t s_prev_lock;

static uint16_t elapsed16(uint32_t start, uint32_t now)
{
    uint32_t elapsed = now - start;
    return (elapsed > 0xFFFFU) ? 0xFFFFU : (uint16_t)elapsed;
}

static void ts_stop_all_to_idle(void)
{
    App_IO_All_Off();
    s_ts_state = TS_IDLE;
    g_lift_state = LIFT_STATE_IDLE;
}

static void thin_scissor_init(void)
{
    s_ts_state = TS_IDLE;
    s_action_start_tick = 0U;
    s_prev_up = 0U;
    s_prev_down = 0U;
    s_prev_lock = 0U;
    g_current_role = LIFT_ROLE_MAIN;
    App_IO_All_Off();
    THIN_LOG_I("THIN", "[THIN] thin_scissor ops init");
}

static void ts_start_up(void)
{
    App_IO_Write(IO_OUT_MOTOR, 1U);
    s_action_start_tick = HAL_GetTick();
    s_ts_state = TS_RISING;
    g_lift_state = LIFT_STATE_RISING;

    App_OpLog_Record(OP_UP_START, OP_RESULT_OK, 0U, NULL, 0U);
    THIN_LOG_I("THIN", "[THIN] UP start motor=ON refill=ON air=OFF");
}

static void ts_poll_up(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t upper_limit = App_IO_Read(IO_IN_UPPER_LIMIT);
    uint8_t up_pressed = App_IO_Read(IO_IN_UP_BUTTON);

    if (upper_limit != 0U) {
        uint16_t dur = elapsed16(s_action_start_tick, now);
        ts_stop_all_to_idle();
        App_OpLog_Record(OP_UP_STOP_LIMIT, OP_RESULT_OK, dur, NULL, 0U);
        THIN_LOG_I("THIN", "[THIN] UP stop upper_limit dur=%u", dur);
    } else if (up_pressed == 0U) {
        uint16_t dur = elapsed16(s_action_start_tick, now);
        ts_stop_all_to_idle();
        App_OpLog_Record(OP_UP_STOP_RELEASE, OP_RESULT_OK, dur, NULL, 0U);
        THIN_LOG_I("THIN", "[THIN] UP stop release up=%u dur=%u", up_pressed, dur);
    }
}

static void ts_start_down(void)
{
    App_IO_Write(IO_OUT_MOTOR, 1U);
    s_action_start_tick = HAL_GetTick();
    s_ts_state = TS_DOWN_PREPARE;
    g_lift_state = LIFT_STATE_DROPPING;

    App_OpLog_Record(OP_DOWN_START, OP_RESULT_OK, 0U, NULL, 0U);
    App_W25Qxx_Stats_Inc_Down(LIFT_ROLE_MAIN);
    THIN_LOG_I("THIN", "[THIN] DOWN start motor=ON");
}

static void ts_force_fast_down(uint8_t upper_limit, uint8_t lock_pressed)
{
    uint8_t was_dropping = (s_ts_state == TS_DROPPING) ? 1U : 0U;

    App_IO_Write(IO_OUT_MOTOR, 0U);
    App_IO_Write(IO_OUT_DROP_VALVE, 1U);
    App_IO_Write(IO_OUT_MAIN_AIR_VALVE, 1U);
    s_ts_state = TS_DROPPING;
    g_lift_state = LIFT_STATE_DROPPING;
    if (was_dropping == 0U) {
        THIN_LOG_I("THIN",
               "[THIN] DOWN forced_fast_down upper=%u lock=%u motor=OFF drop=ON air=ON",
               upper_limit,
               lock_pressed);
    }
}

static void ts_lower_limit_drop_only(void)
{
    App_IO_Write(IO_OUT_MOTOR, 0U);
    App_IO_Write(IO_OUT_DROP_VALVE, 1U);
    App_IO_Write(IO_OUT_MAIN_AIR_VALVE, 0U);
    s_ts_state = TS_DROPPING;
    g_lift_state = LIFT_STATE_DROPPING;
    THIN_LOG_I("THIN", "[THIN] DOWN lower_limit motor=OFF drop=ON air=OFF");
}

static void ts_poll_down(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t down_pressed = App_IO_Read(IO_IN_DOWN_BUTTON);
    uint8_t upper_limit = App_IO_Read(IO_IN_UPPER_LIMIT);
    uint8_t lock_pressed = App_IO_Read(IO_IN_LOCK_BUTTON);
    uint8_t lower_limit = App_IO_Read(IO_IN_LOWER_LIMIT);

    if (down_pressed == 0U) {
        uint16_t dur = elapsed16(s_action_start_tick, now);
        ts_stop_all_to_idle();
        App_OpLog_Record(OP_DOWN_STOP_RELEASE, OP_RESULT_OK, dur, NULL, 0U);
        THIN_LOG_I("THIN", "[THIN] DOWN stop release dur=%u", dur);
        return;
    }

    if (lower_limit != 0U) {
        ts_lower_limit_drop_only();
        return;
    }

    if ((upper_limit != 0U) || (lock_pressed != 0U)) {
        ts_force_fast_down(upper_limit, lock_pressed);
        return;
    }

    if (s_ts_state == TS_DOWN_PREPARE) {
        if ((now - s_action_start_tick) >= g_config.motor_to_valve_delay_ms) {
            App_IO_Write(IO_OUT_MAIN_AIR_VALVE, 1U);
            THIN_LOG_I("THIN", "[THIN] DOWN air=ON");
            s_ts_state = TS_DOWN_HOLD_MOTOR;
        }
    } else if (s_ts_state == TS_DOWN_HOLD_MOTOR) {
        if ((now - s_action_start_tick) >= g_config.motor_hold_ms) {
            App_IO_Write(IO_OUT_MOTOR, 0U);
            App_IO_Write(IO_OUT_DROP_VALVE, 1U);
            s_ts_state = TS_DROPPING;
            THIN_LOG_I("THIN", "[THIN] DOWN motor=OFF drop=ON");
        }
    }
}

static void ts_start_lock(void)
{
    App_IO_Write(IO_OUT_MOTOR, 0U);
    App_IO_Write(IO_OUT_DROP_VALVE, 1U);
    App_IO_Write(IO_OUT_MAIN_AIR_VALVE, 0U);
    s_action_start_tick = HAL_GetTick();
    s_ts_state = TS_LOCKING;
    g_lift_state = LIFT_STATE_LOCKED;

    App_OpLog_Record(OP_LOCK_START, OP_RESULT_OK, 0U, NULL, 0U);
    App_W25Qxx_Stats_Inc_Lock();
    THIN_LOG_I("THIN", "[THIN] LOCK start motor=OFF drop=ON air=OFF");
}

static void ts_poll_lock(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t lock_pressed = App_IO_Read(IO_IN_LOCK_BUTTON);

    if (lock_pressed == 0U) {
        uint16_t dur = elapsed16(s_action_start_tick, now);
        ts_stop_all_to_idle();
        App_OpLog_Record(OP_LOCK_STOP, OP_RESULT_OK, dur, NULL, 0U);
        THIN_LOG_I("THIN", "[THIN] LOCK stop dur=%u", dur);
    } else {
        App_IO_Write(IO_OUT_MOTOR, 0U);
        App_IO_Write(IO_OUT_DROP_VALVE, 1U);
        App_IO_Write(IO_OUT_MAIN_AIR_VALVE, 0U);
    }
}

static void ts_start_refill(void)
{
    App_IO_Write(IO_OUT_MOTOR, 1U);
    App_IO_Write(IO_OUT_DROP_VALVE, 0U);
    App_IO_Write(IO_OUT_MAIN_AIR_VALVE, 0U);
    s_action_start_tick = HAL_GetTick();
    s_ts_state = TS_REFILLING;
    g_lift_state = LIFT_STATE_REFILLING;

    App_OpLog_Record(OP_REFILL_START, OP_RESULT_OK, 0U, NULL, 0U);
    App_W25Qxx_Stats_Inc_Refill();
    THIN_LOG_I("THIN", "[THIN] REFILL start motor=ON air=OFF drop=OFF");
}

static void ts_poll_refill(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t up_pressed = App_IO_Read(IO_IN_UP_BUTTON);
    uint8_t refill_pressed = App_IO_Read(IO_IN_REFILL_BUTTON);
    uint8_t down_pressed = App_IO_Read(IO_IN_DOWN_BUTTON);
    uint8_t lock_pressed = App_IO_Read(IO_IN_LOCK_BUTTON);

    if ((up_pressed == 0U) ||
        (refill_pressed == 0U) ||
        (down_pressed != 0U) ||
        (lock_pressed != 0U)) {
        uint16_t dur = elapsed16(s_action_start_tick, now);
        ts_stop_all_to_idle();
        App_OpLog_Record(OP_REFILL_STOP, OP_RESULT_OK, dur, NULL, 0U);
        THIN_LOG_I("THIN", "[THIN] REFILL stop dur=%u", dur);
    }
}

static void thin_scissor_on_estop(void)
{
    App_IO_All_Off();
    s_ts_state = TS_IDLE;
}

static void thin_scissor_on_photoelectric_blocked(void)
{
    App_IO_All_Off();
    s_ts_state = TS_IDLE;
}

static void thin_scissor_on_clear_alarm(void)
{
    s_ts_state = TS_IDLE;
}

static void thin_scissor_poll(void)
{
    uint8_t up_pressed = App_IO_Read(IO_IN_UP_BUTTON);
    uint8_t down_pressed = App_IO_Read(IO_IN_DOWN_BUTTON);
    uint8_t lock_pressed = App_IO_Read(IO_IN_LOCK_BUTTON);
    uint8_t refill_pressed = App_IO_Read(IO_IN_REFILL_BUTTON);

    if ((up_pressed != 0U) && (down_pressed != 0U)) {
        if (s_ts_state != TS_IDLE) {
            ts_stop_all_to_idle();
            THIN_LOG_W("THIN", "[THIN] invalid input up+down while moving, stop all");
        } else {
            App_IO_All_Off();
        }
        s_prev_down = down_pressed;
        s_prev_lock = lock_pressed;
        s_prev_up = up_pressed;
        return;
    }

    switch (s_ts_state) {
    case TS_IDLE:
        if ((up_pressed != 0U) &&
            (refill_pressed != 0U) &&
            (down_pressed == 0U) &&
            (lock_pressed == 0U)) {
            ts_start_refill();
        } else if ((lock_pressed != 0U) && (s_prev_lock == 0U)) {
            ts_start_lock();
        } else if ((up_pressed != 0U) &&
                   (s_prev_up == 0U) &&
                   (down_pressed == 0U) && (lock_pressed == 0U) &&
                   (App_IO_Read(IO_IN_UPPER_LIMIT) == 0U)) {
            ts_start_up();
        } else if ((down_pressed != 0U) && (s_prev_down == 0U)) {
            ts_start_down();
        }
        break;

    case TS_RISING:
        ts_poll_up();
        break;

    case TS_DOWN_PREPARE:
    case TS_DOWN_HOLD_MOTOR:
    case TS_DROPPING:
        ts_poll_down();
        break;

    case TS_LOCKING:
        ts_poll_lock();
        break;

    case TS_REFILLING:
        ts_poll_refill();
        break;

    default:
        ts_stop_all_to_idle();
        break;
    }

    s_prev_down = down_pressed;
    s_prev_lock = lock_pressed;
    s_prev_up = up_pressed;
}

const lift_ops_t thin_scissor_ops = {
    .init = thin_scissor_init,
    .on_up_pressed = NULL,
    .on_down_pressed = NULL,
    .on_lock_pressed = NULL,
    .on_refill_pressed = NULL,
    .on_estop = thin_scissor_on_estop,
    .on_photoelectric_blocked = thin_scissor_on_photoelectric_blocked,
    .on_clear_alarm = thin_scissor_on_clear_alarm,
    .poll = thin_scissor_poll,
};

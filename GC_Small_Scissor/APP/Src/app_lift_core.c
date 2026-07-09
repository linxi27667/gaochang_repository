/**
 * @file app_lift_core.c
 * @brief Small scissor lift state machine, input debounce, alarms, and output arbitration.
 *
 * Safety boundary:
 * - Inputs only create intent.
 * - The state machine only changes state and requests outputs.
 * - app_lift_apply_outputs() is the only place that writes motion outputs.
 */
#include "app_lift_core.h"

#include "app_io_map.h"
#include "app_op_log.h"
#include "app_w25qxx.h"
#include "elog.h"
#include "main.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define LIFT_TICK_MS                  10U
#define MOTOR_TO_VALVE_DELAY_MS       200U
#define DOWN_MOTOR_HOLD_MS            3000U
#define UP_TIMEOUT_MS                 60000U
#define DOWN_TIMEOUT_MS               60000U

typedef struct {
    uint8_t last_raw;
    uint8_t stable_raw;
    uint32_t last_change_tick;
    uint16_t debounce_ms;
} lift_debounce_t;

static lift_ctx_t s_lift_ctx;
static lift_debounce_t s_lift_db[LIFT_INPUT_COUNT];

static const io_in_id_t s_input_map[LIFT_INPUT_COUNT] = {
    [LIFT_IN_UP] = IO_IN_UP_BUTTON,
    [LIFT_IN_DOWN] = IO_IN_DOWN_BUTTON,
    [LIFT_IN_LOCK] = IO_IN_LOCK_BUTTON,
    [LIFT_IN_ESTOP] = IO_IN_ESTOP,
    [LIFT_IN_UPPER_LIMIT] = IO_IN_UPPER_LIMIT,
    [LIFT_IN_REFILL] = IO_IN_REFILL_BUTTON,
    [LIFT_IN_PHOTO] = IO_IN_PHOTOELECTRIC,
    [LIFT_IN_LOWER_LIMIT] = IO_IN_LOWER_LIMIT,
};

/* 0=raw low is active, 1=raw high is active. Limits/photo open high. */
static const uint8_t s_input_active_level[LIFT_INPUT_COUNT] = {
    [LIFT_IN_UP] = 0U,
    [LIFT_IN_DOWN] = 0U,
    [LIFT_IN_LOCK] = 0U,
    [LIFT_IN_ESTOP] = 0U,
    [LIFT_IN_UPPER_LIMIT] = 1U,
    [LIFT_IN_REFILL] = 0U,
    [LIFT_IN_PHOTO] = 1U,
    [LIFT_IN_LOWER_LIMIT] = 1U,
};

static const uint16_t s_input_debounce_ms[LIFT_INPUT_COUNT] = {
    [LIFT_IN_UP] = 20U,
    [LIFT_IN_DOWN] = 20U,
    [LIFT_IN_LOCK] = 20U,
    [LIFT_IN_ESTOP] = 20U,
    [LIFT_IN_UPPER_LIMIT] = 20U,
    [LIFT_IN_REFILL] = 20U,
    [LIFT_IN_PHOTO] = 50U,
    [LIFT_IN_LOWER_LIMIT] = 20U,
};

static const char *s_input_name[LIFT_INPUT_COUNT] = {
    [LIFT_IN_UP] = "up",
    [LIFT_IN_DOWN] = "down",
    [LIFT_IN_LOCK] = "lock",
    [LIFT_IN_ESTOP] = "estop",
    [LIFT_IN_UPPER_LIMIT] = "upper",
    [LIFT_IN_REFILL] = "refill",
    [LIFT_IN_PHOTO] = "photo",
    [LIFT_IN_LOWER_LIMIT] = "lower",
};

static const char *s_input_pin_name[LIFT_INPUT_COUNT] = {
    [LIFT_IN_UP] = "PG15",
    [LIFT_IN_DOWN] = "PE0",
    [LIFT_IN_LOCK] = "PE1",
    [LIFT_IN_ESTOP] = "PE2",
    [LIFT_IN_UPPER_LIMIT] = "PE5",
    [LIFT_IN_REFILL] = "PE3",
    [LIFT_IN_PHOTO] = "PE8",
    [LIFT_IN_LOWER_LIMIT] = "PE6",
};

static uint8_t input_pressed_from_raw(uint8_t id, uint8_t raw)
{
    return (s_input_active_level[id] == 0U) ? (uint8_t)(raw == 0U) : raw;
}

static const char *input_state_text(uint8_t state)
{
    return state ? "active" : "inactive";
}

static uint8_t input_is_warning(uint8_t id)
{
    return (uint8_t)((id == LIFT_IN_LOCK) ||
                     (id == LIFT_IN_ESTOP) ||
                     (id == LIFT_IN_UPPER_LIMIT) ||
                     (id == LIFT_IN_LOWER_LIMIT) ||
                     (id == LIFT_IN_PHOTO));
}

static const char *input_warning_tag(uint8_t id)
{
    switch (id) {
    case LIFT_IN_UPPER_LIMIT:
    case LIFT_IN_LOWER_LIMIT:
        return "LIMIT";
    case LIFT_IN_ESTOP:
        return "ESTOP";
    case LIFT_IN_PHOTO:
        return "PHOTO";
    case LIFT_IN_LOCK:
        return "LOCK";
    default:
        return "IOMAP";
    }
}

static void log_input_change(uint8_t id, uint8_t old_pressed, uint8_t new_pressed, uint8_t raw)
{
#if IO_MAP_DEBUG == 1
    if ((new_pressed != 0U) && (input_is_warning(id) != 0U)) {
        const char *tag = input_warning_tag(id);
        elog_w(tag, "[%s] triggered source=%s id=%d pin=%s state=%s %u->%u phy=%u active=%u debounce=%u",
               tag,
               s_input_name[id],
               (int)id,
               s_input_pin_name[id],
               input_state_text(new_pressed),
               (unsigned int)old_pressed,
               (unsigned int)new_pressed,
               (unsigned int)raw,
               (unsigned int)s_input_active_level[id],
               (unsigned int)s_input_debounce_ms[id]);
    } else if ((old_pressed != 0U) && (new_pressed == 0U) && (input_is_warning(id) != 0U)) {
        const char *tag = input_warning_tag(id);
        elog_i(tag, "[%s] cleared source=%s id=%d pin=%s state=%s %u->%u phy=%u active=%u debounce=%u",
               tag,
               s_input_name[id],
               (int)id,
               s_input_pin_name[id],
               input_state_text(new_pressed),
               (unsigned int)old_pressed,
               (unsigned int)new_pressed,
               (unsigned int)raw,
               (unsigned int)s_input_active_level[id],
               (unsigned int)s_input_debounce_ms[id]);
    } else {
        elog_i("IOMAP", "[IOMAP] switch changed id=%d name=%s pin=%s state=%s %u->%u phy=%u active=%u debounce=%u",
               (int)id,
               s_input_name[id],
               s_input_pin_name[id],
               input_state_text(new_pressed),
               (unsigned int)old_pressed,
               (unsigned int)new_pressed,
               (unsigned int)raw,
               (unsigned int)s_input_active_level[id],
               (unsigned int)s_input_debounce_ms[id]);
    }
#else
    (void)id;
    (void)old_pressed;
    (void)new_pressed;
    (void)raw;
#endif
}

static uint8_t all_motion_buttons_released(const lift_input_snapshot_t *in)
{
    return (uint8_t)((in->pressed[LIFT_IN_UP] == 0U) &&
                     (in->pressed[LIFT_IN_DOWN] == 0U) &&
                     (in->pressed[LIFT_IN_LOCK] == 0U) &&
                     (in->pressed[LIFT_IN_REFILL] == 0U));
}

static uint8_t photo_blocked_active(const lift_input_snapshot_t *in)
{
    if (in->pressed[LIFT_IN_LOWER_LIMIT] != 0U) {
        return 0U;
    }

    return in->pressed[LIFT_IN_PHOTO];
}

static uint8_t up_refill_mode_active(const lift_input_snapshot_t *in)
{
    return (uint8_t)((in->pressed[LIFT_IN_UP] != 0U) &&
                     (in->pressed[LIFT_IN_REFILL] != 0U) &&
                     (in->pressed[LIFT_IN_DOWN] == 0U) &&
                     (in->pressed[LIFT_IN_LOCK] == 0U));
}

static uint8_t up_allowed_by_limit(const lift_input_snapshot_t *in)
{
    return (uint8_t)(in->pressed[LIFT_IN_UPPER_LIMIT] == 0U);
}

static uint8_t is_motion_state(lift_state_t state)
{
    return (uint8_t)((state == STATE_RISING) ||
                     (state == STATE_DOWN_PREPARE) ||
                     (state == STATE_DOWN_HOLD_MOTOR) ||
                     (state == STATE_DROPPING) ||
                     (state == STATE_LOCKING) ||
                     (state == STATE_REFILL));
}

static void outputs_clear(lift_outputs_t *out)
{
    out->motor_req = 0U;
    out->down_valve_req = 0U;
    out->air_valve_req = 0U;
}

static uint32_t state_elapsed(const lift_ctx_t *ctx)
{
    return HAL_GetTick() - ctx->state_enter_tick;
}

static uint32_t operation_elapsed(const lift_ctx_t *ctx)
{
    return HAL_GetTick() - ctx->operation_start_tick;
}

const char *App_LiftCore_StateName(lift_state_t state)
{
    switch (state) {
    case STATE_INIT: return "init";
    case STATE_IDLE: return "idle";
    case STATE_RISING: return "rising";
    case STATE_DOWN_PREPARE: return "down_prepare";
    case STATE_DOWN_HOLD_MOTOR: return "down_hold_motor";
    case STATE_DROPPING: return "dropping";
    case STATE_LOCKING: return "locking";
    case STATE_REFILL: return "refill";
    case STATE_SAFE_STOP: return "safe_stop";
    case STATE_PHOTO_ALARM: return "photo_alarm";
    case STATE_ESTOP: return "estop";
    case STATE_FAULT: return "fault";
    default: return "unknown";
    }
}

static void transition_to(lift_ctx_t *ctx, lift_state_t new_state, const char *reason)
{
    lift_state_t old_state = ctx->current_state;

    if (reason == NULL) {
        reason = "none";
    }

    if (old_state == new_state) {
        return;
    }

    ctx->current_state = new_state;
    ctx->state_enter_tick = HAL_GetTick();
    ctx->last_stop_reason = reason;

    if (is_motion_state(new_state)) {
        ctx->operation_start_tick = ctx->state_enter_tick;
    }

    elog_i("LIFT",
           "[LIFT] state %s -> %s reason=%s up=%u down=%u lock=%u refill=%u estop=%u photo=%u lower=%u upper=%u out=%u/%u/%u",
           App_LiftCore_StateName(old_state),
           App_LiftCore_StateName(new_state),
           reason,
           ctx->input.pressed[LIFT_IN_UP],
           ctx->input.pressed[LIFT_IN_DOWN],
           ctx->input.pressed[LIFT_IN_LOCK],
           ctx->input.pressed[LIFT_IN_REFILL],
           ctx->input.pressed[LIFT_IN_ESTOP],
           ctx->input.pressed[LIFT_IN_PHOTO],
           ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
           ctx->input.pressed[LIFT_IN_UPPER_LIMIT],
           ctx->output_actual.motor_on,
           ctx->output_actual.down_valve_on,
           ctx->output_actual.air_valve_on);
}

static void record_stop_event(const char *event, app_op_result_t result)
{
    App_OpLog_Record(event, result, operation_elapsed(&s_lift_ctx), s_lift_ctx.last_stop_reason);
}

/*
 * Read every lift input once, debounce it, normalize it into "pressed/active",
 * and log only edge changes. No output decision is made here.
 */
static void collect_inputs(lift_ctx_t *ctx)
{
    uint32_t now = HAL_GetTick();

    for (uint8_t i = 0U; i < LIFT_INPUT_COUNT; i++) {
        uint8_t raw = App_IO_Read_Raw(s_input_map[i]);
        lift_debounce_t *db = &s_lift_db[i];

        ctx->input.raw[i] = raw;

        if (raw != db->last_raw) {
            db->last_raw = raw;
            db->last_change_tick = now;
        } else if (raw != db->stable_raw) {
            if ((now - db->last_change_tick) >= db->debounce_ms) {
                uint8_t old_pressed = ctx->input.pressed[i];
                db->stable_raw = raw;
                ctx->input.debounced[i] = raw;
                ctx->input.pressed[i] = input_pressed_from_raw(i, raw);
                ctx->last_input_tick = now;

                if (old_pressed != ctx->input.pressed[i]) {
                    log_input_change(i, old_pressed, ctx->input.pressed[i], raw);
                }
            }
        }
    }
}

static void init_input_debounce(lift_ctx_t *ctx)
{
    for (uint8_t i = 0U; i < LIFT_INPUT_COUNT; i++) {
        uint8_t raw = App_IO_Read_Raw(s_input_map[i]);
        s_lift_db[i].last_raw = raw;
        s_lift_db[i].stable_raw = raw;
        s_lift_db[i].last_change_tick = HAL_GetTick();
        s_lift_db[i].debounce_ms = s_input_debounce_ms[i];
        ctx->input.raw[i] = raw;
        ctx->input.debounced[i] = raw;
        ctx->input.pressed[i] = input_pressed_from_raw(i, raw);
    }
}

static void enter_safe_stop(lift_ctx_t *ctx, const char *reason)
{
    outputs_clear(&ctx->output_req);
    transition_to(ctx, STATE_SAFE_STOP, reason);
}

static uint8_t remote_lock_active(lift_ctx_t *ctx)
{
    if (ctx->remote_locked == 0U) {
        return 0U;
    }

    outputs_clear(&ctx->output_req);
    if (is_motion_state(ctx->current_state)) {
        App_OpLog_Record("EVENT_REMOTE_LOCK_STOP", APP_OP_RESULT_FAILED, operation_elapsed(ctx), "remote_locked");
        transition_to(ctx, STATE_SAFE_STOP, "remote_locked");
    }

    return 1U;
}

/*
 * Highest-priority pre-state-machine safety checks.
 * Estop, photo alarm, and conflicting buttons can stop motion before the
 * state machine is allowed to request new outputs.
 */
static uint8_t safety_guard_check(lift_ctx_t *ctx)
{
    lift_input_snapshot_t *in = &ctx->input;

    if (remote_lock_active(ctx) != 0U) {
        return 1U;
    }

    if (in->pressed[LIFT_IN_ESTOP] != 0U) {
        if (ctx->alarm.estop_active == 0U) {
            ctx->alarm.estop_active = 1U;
            ctx->estop_count++;
            App_W25Qxx_Stats_Inc_Estop();
            App_OpLog_Record("EVENT_ESTOP", APP_OP_RESULT_FAILED, operation_elapsed(ctx), "estop_active");
            App_IO_LogSnapshot("estop_trigger");
            elog_w("CORE", "[CORE] E-STOP triggered");
        }
        outputs_clear(&ctx->output_req);
        transition_to(ctx, STATE_ESTOP, "estop");
        return 1U;
    }

    if (ctx->current_state == STATE_ESTOP) {
        outputs_clear(&ctx->output_req);
        if ((ctx->alarm.estop_active != 0U) &&
            all_motion_buttons_released(in) &&
            (photo_blocked_active(in) == 0U)) {
            ctx->alarm.estop_active = 0U;
        App_OpLog_Record("EVENT_ESTOP_RECOVER", APP_OP_RESULT_OK, 0U, "buttons_released");
            App_IO_LogSnapshot("estop_release");
            elog_i("CORE", "[CORE] E-STOP released, back to IDLE");
            transition_to(ctx, STATE_IDLE, "estop_recovered");
        }
        return 1U;
    }

    if ((in->pressed[LIFT_IN_LOWER_LIMIT] != 0U) &&
        (ctx->current_state == STATE_PHOTO_ALARM)) {
        outputs_clear(&ctx->output_req);
        ctx->remote_clear_photo_req = 0U;
        ctx->alarm.photo_alarm_latched = 0U;
        ctx->alarm.photo_alarm_requires_remote_clear = 0U;
        App_OpLog_Record("EVENT_PHOTO_ALARM_CLEAR", APP_OP_RESULT_OK, 0U, "lower_limit");
        elog_i("CORE", "[CORE] Photo alarm cleared by lower limit shield");
        transition_to(ctx, STATE_IDLE, "photo_alarm_cleared_by_lower");
        return 1U;
    }

    if (photo_blocked_active(in) != 0U) {
        if (ctx->alarm.photo_alarm_latched == 0U) {
            ctx->alarm.photo_alarm_latched = 1U;
            ctx->alarm.photo_alarm_requires_remote_clear = is_motion_state(ctx->current_state);
            ctx->remote_clear_photo_req = 0U;
            ctx->photo_alarm_count++;
            App_W25Qxx_Stats_Inc_PhotoAlarm();
            App_OpLog_Record("EVENT_PHOTO_ALARM", APP_OP_RESULT_FAILED, operation_elapsed(ctx), "photo_blocked");
            App_IO_LogSnapshot("photo_alarm");
            elog_w("CORE", "[CORE] Photoelectric blocked, alarm triggered remote_clear=%u",
                   ctx->alarm.photo_alarm_requires_remote_clear);
        }
        outputs_clear(&ctx->output_req);
        transition_to(ctx, STATE_PHOTO_ALARM, "photo_alarm");
        return 1U;
    }

    if (ctx->current_state == STATE_PHOTO_ALARM) {
        outputs_clear(&ctx->output_req);
        if ((ctx->alarm.photo_alarm_latched != 0U) &&
            (photo_blocked_active(in) == 0U) &&
            ((ctx->remote_clear_photo_req != 0U) ||
             (ctx->alarm.photo_alarm_requires_remote_clear == 0U)) &&
            all_motion_buttons_released(in)) {
            uint8_t was_remote_clear = ctx->remote_clear_photo_req;
            ctx->remote_clear_photo_req = 0U;
            ctx->alarm.photo_alarm_latched = 0U;
            ctx->alarm.photo_alarm_requires_remote_clear = 0U;
            App_OpLog_Record("EVENT_PHOTO_ALARM_CLEAR", APP_OP_RESULT_OK, 0U,
                             (was_remote_clear != 0U) ? "remote_clear" : "photo_released");
            if (was_remote_clear != 0U) {
                elog_i("CORE", "[CORE] Photo alarm cleared by remote");
            } else {
                elog_i("CORE", "[CORE] Photo alarm auto-cleared by photo release");
            }
            transition_to(ctx, STATE_IDLE,
                          (was_remote_clear != 0U) ? "photo_alarm_cleared" : "photo_alarm_auto_cleared");
        }
        return 1U;
    }

    if ((in->pressed[LIFT_IN_UP] != 0U) && (in->pressed[LIFT_IN_DOWN] != 0U)) {
        if (ctx->alarm.invalid_input_latched == 0U) {
            ctx->alarm.invalid_input_latched = 1U;
            App_OpLog_Record("EVENT_INVALID_INPUT", APP_OP_RESULT_FAILED, operation_elapsed(ctx), "up_down_same_time");
            elog_w("CTRL", "[CTRL] Dual key conflict: motion inhibited");
        }
        enter_safe_stop(ctx, "up_down_same_time");
        return 1U;
    }

    if ((ctx->current_state == STATE_SAFE_STOP) &&
        (ctx->alarm.invalid_input_latched != 0U)) {
        outputs_clear(&ctx->output_req);
        if (all_motion_buttons_released(in)) {
            ctx->alarm.invalid_input_latched = 0U;
            transition_to(ctx, STATE_IDLE, "invalid_input_released");
        }
        return 1U;
    }

    return 0U;
}

static uint8_t state_input_matches(const lift_ctx_t *ctx)
{
    const lift_input_snapshot_t *in = &ctx->input;

    switch (ctx->current_state) {
    case STATE_RISING:
        return in->pressed[LIFT_IN_UP];
    case STATE_DOWN_PREPARE:
    case STATE_DOWN_HOLD_MOTOR:
    case STATE_DROPPING:
        return in->pressed[LIFT_IN_DOWN];
    case STATE_LOCKING:
        return in->pressed[LIFT_IN_LOCK];
    case STATE_REFILL:
        return up_refill_mode_active(in);
    default:
        return 1U;
    }
}

static uint8_t operation_timed_out(const lift_ctx_t *ctx)
{
    uint32_t elapsed = operation_elapsed(ctx);

    switch (ctx->current_state) {
    case STATE_RISING:
        return (elapsed > UP_TIMEOUT_MS);
    case STATE_DOWN_PREPARE:
    case STATE_DOWN_HOLD_MOTOR:
    case STATE_DROPPING:
        return (elapsed > DOWN_TIMEOUT_MS);
    default:
        return 0U;
    }
}

static void handle_timeout_if_needed(lift_ctx_t *ctx)
{
    if (operation_timed_out(ctx) == 0U) {
        return;
    }

    ctx->alarm.timeout_latched = 1U;
    ctx->alarm.fault_latched = 1U;
    outputs_clear(&ctx->output_req);
    App_OpLog_Record("EVENT_OPERATION_TIMEOUT", APP_OP_RESULT_FAILED, operation_elapsed(ctx), "operation_timeout");
    elog_e("SAFETY", "[SAFETY] operation timeout state=%s", App_LiftCore_StateName(ctx->current_state));
    transition_to(ctx, STATE_FAULT, "operation_timeout");
}

static void stop_to_idle(lift_ctx_t *ctx, const char *event, const char *reason)
{
    outputs_clear(&ctx->output_req);
    ctx->last_stop_reason = reason;
    record_stop_event(event, APP_OP_RESULT_OK);
    transition_to(ctx, STATE_IDLE, reason);
}

/*
 * Main lift state machine.
 * It consumes the debounced snapshot and changes state/counters only; it never
 * writes PF8/PF9/PD8 directly.
 */
static void state_machine_step(lift_ctx_t *ctx)
{
    lift_input_snapshot_t *in = &ctx->input;

    if (ctx->remote_locked != 0U) {
        outputs_clear(&ctx->output_req);
        return;
    }

    handle_timeout_if_needed(ctx);
    if (ctx->current_state == STATE_FAULT) {
        return;
    }

    switch (ctx->current_state) {
    case STATE_INIT:
        if (all_motion_buttons_released(in) &&
            (in->pressed[LIFT_IN_ESTOP] == 0U) &&
            (photo_blocked_active(in) == 0U)) {
            transition_to(ctx, STATE_IDLE, "boot_safe");
        } else {
            transition_to(ctx, STATE_SAFE_STOP, "boot_inputs_not_safe");
        }
        break;

    case STATE_SAFE_STOP:
        if (all_motion_buttons_released(in)) {
            transition_to(ctx, STATE_IDLE, "safe_stop_released");
        }
        break;

    case STATE_IDLE:
        if (up_refill_mode_active(in) != 0U) {
            ctx->refill_count++;
            App_W25Qxx_Stats_Inc_Refill();
            App_OpLog_Record("op_refill_start", APP_OP_RESULT_OK, 0U, "up_refill");
            transition_to(ctx, STATE_REFILL, "up_refill_button");
        } else if ((in->pressed[LIFT_IN_UP] != 0U) &&
                   (in->pressed[LIFT_IN_DOWN] == 0U) &&
                   (up_allowed_by_limit(in) != 0U)) {
            ctx->up_count++;
            App_W25Qxx_Stats_Inc_Up(LIFT_ROLE_MAIN);
            App_OpLog_Record("op_up_start", APP_OP_RESULT_OK, 0U, "up");
            transition_to(ctx, STATE_RISING, "up_button");
        } else if ((in->pressed[LIFT_IN_DOWN] != 0U) &&
                   (in->pressed[LIFT_IN_UP] == 0U)) {
            ctx->down_count++;
            App_W25Qxx_Stats_Inc_Down(LIFT_ROLE_MAIN);
            App_OpLog_Record("op_down_start", APP_OP_RESULT_OK, 0U, "PE1");
            transition_to(ctx, STATE_DOWN_PREPARE, "down_button");
        } else if (in->pressed[LIFT_IN_LOCK] != 0U) {
            ctx->lock_count++;
            App_W25Qxx_Stats_Inc_Lock();
            App_OpLog_Record("op_lock_start", APP_OP_RESULT_OK, 0U, "PE2");
            transition_to(ctx, STATE_LOCKING, "lock_button");
        }
        break;

    case STATE_RISING:
        if (up_refill_mode_active(in) != 0U) {
            ctx->refill_count++;
            App_W25Qxx_Stats_Inc_Refill();
            App_OpLog_Record("op_refill_start", APP_OP_RESULT_OK, 0U, "up_refill");
            transition_to(ctx, STATE_REFILL, "up_refill_button");
        } else if (in->pressed[LIFT_IN_UP] == 0U) {
            stop_to_idle(ctx, "op_up_stop_release", "up_release");
        } else if (in->pressed[LIFT_IN_UPPER_LIMIT] != 0U) {
            stop_to_idle(ctx, "op_up_stop_limit", "upper_limit");
        }
        break;

    case STATE_DOWN_PREPARE:
        if (in->pressed[LIFT_IN_DOWN] == 0U) {
            stop_to_idle(ctx, "op_down_stop_release", "down_release");
        } else if ((in->pressed[LIFT_IN_UPPER_LIMIT] != 0U) ||
                   (in->pressed[LIFT_IN_LOCK] != 0U)) {
            transition_to(ctx, STATE_DROPPING, "forced_fast_down");
        } else if (state_elapsed(ctx) >= MOTOR_TO_VALVE_DELAY_MS) {
            ctx->down_air_on_tick = HAL_GetTick();
            transition_to(ctx, STATE_DOWN_HOLD_MOTOR, "down_air_on");
        }
        break;

    case STATE_DOWN_HOLD_MOTOR:
        if (in->pressed[LIFT_IN_DOWN] == 0U) {
            stop_to_idle(ctx, "op_down_stop_release", "down_release");
        } else if ((in->pressed[LIFT_IN_UPPER_LIMIT] != 0U) ||
                   (in->pressed[LIFT_IN_LOCK] != 0U)) {
            transition_to(ctx, STATE_DROPPING, "forced_fast_down");
        } else if ((HAL_GetTick() - ctx->down_air_on_tick) >= DOWN_MOTOR_HOLD_MS) {
            transition_to(ctx, STATE_DROPPING, "down_motor_hold_done");
        }
        break;

    case STATE_DROPPING:
        if (in->pressed[LIFT_IN_DOWN] == 0U) {
            stop_to_idle(ctx, "op_down_stop_release", "down_release");
        }
        break;

    case STATE_LOCKING:
        if (in->pressed[LIFT_IN_LOCK] == 0U) {
            stop_to_idle(ctx, "op_lock_stop", "lock_release");
        }
        break;

    case STATE_REFILL:
        if (up_refill_mode_active(in) == 0U) {
            stop_to_idle(ctx, "op_refill_stop", "refill_release_or_conflict");
        }
        break;

    case STATE_FAULT:
        outputs_clear(&ctx->output_req);
        break;

    default:
        transition_to(ctx, STATE_SAFE_STOP, "unknown_state");
        break;
    }
}

/*
 * Translate the current state into requested outputs. The returned request is
 * still not trusted until safety_guard_before_output() checks it again.
 */
lift_outputs_t lift_output_arbitrate(lift_ctx_t *ctx)
{
    lift_outputs_t out;
    uint32_t elapsed;

    outputs_clear(&out);

    switch (ctx->current_state) {
    case STATE_RISING:
        out.motor_req = 1U;
        elapsed = state_elapsed(ctx);
        if (elapsed >= MOTOR_TO_VALVE_DELAY_MS) {
            out.air_valve_req = 1U;
        }
        break;

    case STATE_DOWN_PREPARE:
        out.motor_req = 1U;
        break;

    case STATE_DOWN_HOLD_MOTOR:
        out.motor_req = 1U;
        out.air_valve_req = 1U;
        break;

    case STATE_DROPPING:
        out.down_valve_req = 1U;
        out.air_valve_req = 1U;
        break;

    case STATE_LOCKING:
        out.down_valve_req = 1U;
        out.air_valve_req = 1U;
        break;

    case STATE_REFILL:
        out.motor_req = 1U;
        out.air_valve_req = 1U;
        break;

    default:
        break;
    }

    ctx->output_req = out;
    return out;
}

static uint8_t outputs_illegal_for_state(const lift_ctx_t *ctx, const lift_outputs_t *out)
{
    if ((out->motor_req != 0U) && (out->down_valve_req != 0U)) {
        return 1U;
    }

    switch (ctx->current_state) {
    case STATE_IDLE:
    case STATE_SAFE_STOP:
    case STATE_PHOTO_ALARM:
    case STATE_ESTOP:
    case STATE_FAULT:
        return (uint8_t)((out->motor_req != 0U) ||
                         (out->down_valve_req != 0U) ||
                         (out->air_valve_req != 0U));
    case STATE_RISING:
        return (uint8_t)(out->down_valve_req != 0U);
    case STATE_DOWN_PREPARE:
        return (uint8_t)((out->down_valve_req != 0U) || (out->air_valve_req != 0U));
    case STATE_DOWN_HOLD_MOTOR:
        return (uint8_t)(out->down_valve_req != 0U);
    case STATE_DROPPING:
        return (uint8_t)(out->motor_req != 0U);
    case STATE_LOCKING:
        return (uint8_t)(out->motor_req != 0U);
    case STATE_REFILL:
        return (uint8_t)(out->down_valve_req != 0U);
    default:
        return 1U;
    }
}

/*
 * Final safety gate before touching hardware outputs.
 * Any latched alarm, state/input mismatch, or illegal output combination
 * collapses the request to all-off.
 */
static lift_outputs_t safety_guard_before_output(lift_ctx_t *ctx, lift_outputs_t requested)
{
    lift_outputs_t safe = requested;

    if (ctx->remote_locked != 0U) {
        outputs_clear(&safe);
        return safe;
    }

    if ((ctx->alarm.estop_active != 0U) ||
        (ctx->alarm.photo_alarm_latched != 0U) ||
        (ctx->alarm.fault_latched != 0U) ||
        (ctx->current_state == STATE_FAULT)) {
        outputs_clear(&safe);
        return safe;
    }

    if (state_input_matches(ctx) == 0U) {
        outputs_clear(&safe);
        ctx->alarm.output_mismatch_latched = 1U;
        App_OpLog_Record("EVENT_STATE_INPUT_MISMATCH", APP_OP_RESULT_FAILED, operation_elapsed(ctx), "state_input_mismatch");
        elog_w("SAFETY", "[SAFETY] state/input mismatch state=%s", App_LiftCore_StateName(ctx->current_state));
        transition_to(ctx, STATE_SAFE_STOP, "state_input_mismatch");
        return safe;
    }

    if (outputs_illegal_for_state(ctx, &safe) != 0U) {
        outputs_clear(&safe);
        ctx->alarm.output_mismatch_latched = 1U;
        App_OpLog_Record("EVENT_OUTPUT_ILLEGAL", APP_OP_RESULT_FAILED, operation_elapsed(ctx), "illegal_output_combo");
        elog_e("SAFETY",
               "[SAFETY] illegal output combo state=%s req=%u/%u/%u",
               App_LiftCore_StateName(ctx->current_state),
               requested.motor_req,
               requested.down_valve_req,
               requested.air_valve_req);
        transition_to(ctx, STATE_FAULT, "illegal_output_combo");
    }

    return safe;
}

/*
 * The only motion-output writer in the lift core. Callers must pass the result
 * of safety_guard_before_output(), not a raw state-machine request.
 */
void app_lift_apply_outputs(lift_ctx_t *ctx, const lift_outputs_t *safe_outputs)
{
    if ((ctx == NULL) || (safe_outputs == NULL)) {
        return;
    }

    if (ctx->output_actual.motor_on != safe_outputs->motor_req) {
        elog_i("OUT", "[OUT] PF8 motor %s", safe_outputs->motor_req ? "ON" : "OFF");
    }
    if (ctx->output_actual.down_valve_on != safe_outputs->down_valve_req) {
        elog_i("OUT", "[OUT] PF9 down_valve %s", safe_outputs->down_valve_req ? "ON" : "OFF");
    }
    if (ctx->output_actual.air_valve_on != safe_outputs->air_valve_req) {
        elog_i("OUT", "[OUT] PD8 air_valve %s", safe_outputs->air_valve_req ? "ON" : "OFF");
    }

    App_IO_Write(IO_OUT_MOTOR, safe_outputs->motor_req);
    App_IO_Write(IO_OUT_DROP_VALVE, safe_outputs->down_valve_req);
    App_IO_Write(IO_OUT_AIR_VALVE, safe_outputs->air_valve_req);

    ctx->output_actual.motor_on = safe_outputs->motor_req;
    ctx->output_actual.down_valve_on = safe_outputs->down_valve_req;
    ctx->output_actual.air_valve_on = safe_outputs->air_valve_req;
    ctx->last_output_tick = HAL_GetTick();
}

void App_LiftCore_Init(product_type_t type)
{
    memset(&s_lift_ctx, 0, sizeof(s_lift_ctx));
    s_lift_ctx.product_type = type;
    s_lift_ctx.current_state = STATE_INIT;
    s_lift_ctx.state_enter_tick = HAL_GetTick();
    s_lift_ctx.operation_start_tick = s_lift_ctx.state_enter_tick;
    s_lift_ctx.last_stop_reason = "boot";

    App_IO_All_Off();
    init_input_debounce(&s_lift_ctx);

    if (s_lift_ctx.input.pressed[LIFT_IN_ESTOP] != 0U) {
        s_lift_ctx.alarm.estop_active = 1U;
        s_lift_ctx.current_state = STATE_ESTOP;
        s_lift_ctx.last_stop_reason = "boot_estop";
    } else if (photo_blocked_active(&s_lift_ctx.input) != 0U) {
        s_lift_ctx.alarm.photo_alarm_latched = 1U;
        s_lift_ctx.alarm.photo_alarm_requires_remote_clear = 0U;
        s_lift_ctx.current_state = STATE_PHOTO_ALARM;
        s_lift_ctx.last_stop_reason = "boot_photo";
    } else if (all_motion_buttons_released(&s_lift_ctx.input)) {
        s_lift_ctx.current_state = STATE_IDLE;
        s_lift_ctx.last_stop_reason = "boot_safe";
    } else {
        s_lift_ctx.current_state = STATE_SAFE_STOP;
        s_lift_ctx.last_stop_reason = "boot_buttons_pressed";
    }

    elog_i("CORE",
           "[CORE] Init done, product=%s state=%s up=%u down=%u lock=%u refill=%u estop=%u photo=%u lower=%u upper=%u",
           App_Product_TypeName(type),
           App_LiftCore_StateName(s_lift_ctx.current_state),
           s_lift_ctx.input.pressed[LIFT_IN_UP],
           s_lift_ctx.input.pressed[LIFT_IN_DOWN],
           s_lift_ctx.input.pressed[LIFT_IN_LOCK],
           s_lift_ctx.input.pressed[LIFT_IN_REFILL],
           s_lift_ctx.input.pressed[LIFT_IN_ESTOP],
           s_lift_ctx.input.pressed[LIFT_IN_PHOTO],
           s_lift_ctx.input.pressed[LIFT_IN_LOWER_LIMIT],
           s_lift_ctx.input.pressed[LIFT_IN_UPPER_LIMIT]);
}

/*
 * 10 ms control pipeline:
 * collect inputs -> pre-check safety -> state machine -> output request ->
 * final safety gate -> hardware output write.
 */
void App_LiftCore_Task(void)
{
    lift_outputs_t requested;
    lift_outputs_t safe;

    collect_inputs(&s_lift_ctx);

    if (safety_guard_check(&s_lift_ctx) == 0U) {
        state_machine_step(&s_lift_ctx);
    }

    requested = lift_output_arbitrate(&s_lift_ctx);
    safe = safety_guard_before_output(&s_lift_ctx, requested);
    app_lift_apply_outputs(&s_lift_ctx, &safe);
}

void App_LiftCore_SetRemoteLock(uint8_t locked)
{
    uint8_t next = (locked != 0U) ? 1U : 0U;

    if (s_lift_ctx.remote_locked == next) {
        return;
    }

    s_lift_ctx.remote_locked = next;
    if (next != 0U) {
        outputs_clear(&s_lift_ctx.output_req);
        App_IO_All_Off();
        s_lift_ctx.output_actual.motor_on = 0U;
        s_lift_ctx.output_actual.down_valve_on = 0U;
        s_lift_ctx.output_actual.air_valve_on = 0U;
        if (is_motion_state(s_lift_ctx.current_state)) {
            transition_to(&s_lift_ctx, STATE_SAFE_STOP, "remote_locked");
        }
        App_OpLog_Record("remote_lock", APP_OP_RESULT_OK, 0U, "remote");
        elog_i("CORE", "[CORE] Remote lock=%u", next);
    } else {
        App_OpLog_Record("remote_unlock", APP_OP_RESULT_OK, 0U, "remote");
        elog_i("CORE", "[CORE] Remote lock=%u", next);
    }
}

void App_LiftCore_RequestClearPhotoAlarm(const char *source)
{
    if (source == NULL) {
        source = "remote";
    }

    if ((s_lift_ctx.current_state == STATE_PHOTO_ALARM) &&
        (s_lift_ctx.alarm.photo_alarm_latched != 0U) &&
        (s_lift_ctx.alarm.photo_alarm_requires_remote_clear != 0U)) {
        s_lift_ctx.remote_clear_photo_req = 1U;
        App_OpLog_Record("clear_alarm_request", APP_OP_RESULT_OK, 0U, source);
        elog_i("CORE", "[CORE] Photo alarm clear requested by %s", source);
    } else {
        s_lift_ctx.remote_clear_photo_req = 0U;
        App_OpLog_Record("clear_alarm_ignored", APP_OP_RESULT_FAILED, 0U, source);
        elog_i("CORE", "[CORE] Photo alarm clear ignored by %s state=%s latched=%u remote_need=%u",
               source,
               App_LiftCore_StateName(s_lift_ctx.current_state),
               s_lift_ctx.alarm.photo_alarm_latched,
               s_lift_ctx.alarm.photo_alarm_requires_remote_clear);
    }
}

void App_LiftCore_RequestClearFault(const char *source)
{
    if (source == NULL) {
        source = "remote";
    }

    if ((s_lift_ctx.current_state == STATE_FAULT) &&
        all_motion_buttons_released(&s_lift_ctx.input) &&
        (s_lift_ctx.input.pressed[LIFT_IN_ESTOP] == 0U) &&
        (photo_blocked_active(&s_lift_ctx.input) == 0U)) {
        s_lift_ctx.alarm.fault_latched = 0U;
        s_lift_ctx.alarm.timeout_latched = 0U;
        s_lift_ctx.alarm.output_mismatch_latched = 0U;
        transition_to(&s_lift_ctx, STATE_IDLE, "fault_clear");
        App_OpLog_Record("fault_clear", APP_OP_RESULT_OK, 0U, source);
    } else {
        App_OpLog_Record("fault_clear_denied", APP_OP_RESULT_FAILED, 0U, source);
    }
}

const lift_ctx_t *App_LiftCore_GetContext(void)
{
    return &s_lift_ctx;
}

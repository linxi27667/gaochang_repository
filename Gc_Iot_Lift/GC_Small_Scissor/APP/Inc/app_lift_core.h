/**
 * @file app_lift_core.h
 * @brief Safety-first lift control core interface for GC Small Scissor.
 *
 * Buttons and sensors are exposed as debounced input snapshots. The state
 * machine creates output requests, and the final safety arbiter decides which
 * PF8/PD9/PD8 outputs may actually be written.
 */
#ifndef __APP_LIFT_CORE_H__
#define __APP_LIFT_CORE_H__

#include <stdint.h>
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIFT_INPUT_COUNT 8U

typedef enum {
    STATE_INIT = 0,
    STATE_IDLE,
    STATE_RISING,
    STATE_DOWN_PREPARE,
    STATE_DOWN_HOLD_MOTOR,
    STATE_DROPPING,
    STATE_LOCKING,
    STATE_REFILL,
    STATE_SAFE_STOP,
    STATE_PHOTO_ALARM,
    STATE_ESTOP,
    STATE_FAULT
} lift_state_t;

typedef enum {
    LIFT_IN_UP = 0,
    LIFT_IN_DOWN,
    LIFT_IN_LOCK,
    LIFT_IN_ESTOP,
    LIFT_IN_UPPER_LIMIT,
    LIFT_IN_REFILL,
    LIFT_IN_PHOTO,
    LIFT_IN_LOWER_LIMIT
} lift_input_id_t;

typedef struct {
    uint8_t raw[LIFT_INPUT_COUNT];
    uint8_t debounced[LIFT_INPUT_COUNT];
    uint8_t pressed[LIFT_INPUT_COUNT];
} lift_input_snapshot_t;

typedef struct {
    uint8_t motor_req;
    uint8_t down_valve_req;
    uint8_t air_valve_req;
} lift_outputs_t;

typedef struct {
    uint8_t motor_on;
    uint8_t down_valve_on;
    uint8_t air_valve_on;
} lift_output_actual_t;

typedef struct {
    uint8_t estop_active;
    uint8_t photo_alarm_latched;
    uint8_t photo_alarm_requires_remote_clear;
    uint8_t invalid_input_latched;
    uint8_t output_mismatch_latched;
    uint8_t timeout_latched;
    uint8_t fault_latched;
} lift_alarm_t;

typedef struct {
    product_type_t product_type;
    lift_state_t current_state;
    lift_input_snapshot_t input;
    lift_outputs_t output_req;
    lift_output_actual_t output_actual;
    lift_alarm_t alarm;
    uint32_t state_enter_tick;
    uint32_t last_input_tick;
    uint32_t last_output_tick;
    uint32_t operation_start_tick;
    uint32_t down_air_on_tick;
    uint32_t up_count;
    uint32_t down_count;
    uint32_t lock_count;
    uint32_t refill_count;
    uint32_t estop_count;
    uint32_t photo_alarm_count;
    const char *last_stop_reason;
    uint8_t remote_clear_photo_req;
    uint8_t remote_locked;
} lift_ctx_t;

void App_LiftCore_Init(product_type_t type);
void App_LiftCore_Task(void);
void App_LiftCore_SetRemoteLock(uint8_t locked);
uint8_t App_LiftCore_RequestClearPhotoAlarm(const char *source);
void App_LiftCore_CancelClearPhotoAlarm(void);
void App_LiftCore_RequestClearFault(const char *source);

const lift_ctx_t *App_LiftCore_GetContext(void);
const char *App_LiftCore_StateName(lift_state_t state);

lift_outputs_t lift_output_arbitrate(lift_ctx_t *ctx);
void app_lift_apply_outputs(lift_ctx_t *ctx, const lift_outputs_t *safe_outputs);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LIFT_CORE_H__ */

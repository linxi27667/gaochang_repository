/**
 * @file dri_motor.c
 * @brief Read-only output status helpers.
 *
 * This file intentionally does not provide direct output write functions.
 * PF8/PF9/PD8 must only be written after APP safety arbitration.
 */
#include "dri_motor.h"

#include <string.h>

void Dri_Motor_GetActual(lift_output_actual_t *out)
{
    const lift_ctx_t *ctx;

    if (out == 0) {
        return;
    }

    ctx = App_LiftCore_GetContext();
    if (ctx == 0) {
        memset(out, 0, sizeof(*out));
        return;
    }

    *out = ctx->output_actual;
}

uint8_t Dri_Motor_IsAnyOutputOn(void)
{
    lift_output_actual_t out;

    Dri_Motor_GetActual(&out);
    return (uint8_t)((out.motor_on != 0U) ||
                     (out.down_valve_on != 0U) ||
                     (out.air_valve_on != 0U));
}

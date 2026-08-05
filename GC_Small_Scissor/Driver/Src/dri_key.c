/**
 * @file dri_key.c
 * @brief Read-only accessors for the latest APP input snapshot.
 */
#include "dri_key.h"

const lift_input_snapshot_t *Dri_Key_GetSnapshot(void)
{
    const lift_ctx_t *ctx = App_LiftCore_GetContext();

    return (ctx != 0) ? &ctx->input : 0;
}

uint8_t Dri_Key_IsAnyMotionButtonPressed(void)
{
    const lift_input_snapshot_t *in = Dri_Key_GetSnapshot();

    if (in == 0) {
        return 0U;
    }

    return (uint8_t)((in->pressed[LIFT_IN_UP] != 0U) ||
                     (in->pressed[LIFT_IN_DOWN] != 0U) ||
                     (in->pressed[LIFT_IN_LOCK] != 0U) ||
                     (in->pressed[LIFT_IN_REFILL] != 0U));
}

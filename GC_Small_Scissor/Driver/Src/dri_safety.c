/**
 * @file dri_safety.c
 * @brief Read-only alarm helpers backed by App_LiftCore context.
 */
#include "dri_safety.h"

const lift_alarm_t *Dri_Safety_GetAlarm(void)
{
    const lift_ctx_t *ctx = App_LiftCore_GetContext();

    return (ctx != 0) ? &ctx->alarm : 0;
}

uint8_t Dri_Safety_IsEmergencyActive(void)
{
    const lift_alarm_t *alarm = Dri_Safety_GetAlarm();

    return ((alarm != 0) && (alarm->estop_active != 0U)) ? 1U : 0U;
}

uint8_t Dri_Safety_IsAlarmLatched(void)
{
    const lift_alarm_t *alarm = Dri_Safety_GetAlarm();

    if (alarm == 0) {
        return 0U;
    }

    return (uint8_t)((alarm->estop_active != 0U) ||
                     (alarm->photo_alarm_latched != 0U) ||
                     (alarm->invalid_input_latched != 0U) ||
                     (alarm->output_mismatch_latched != 0U) ||
                     (alarm->timeout_latched != 0U) ||
                     (alarm->fault_latched != 0U));
}

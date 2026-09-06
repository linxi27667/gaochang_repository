/**
 * @file dri_safety.h
 * @brief Read-only driver facade for lift alarm and safety state.
 */
#ifndef __DRI_SAFETY_H__
#define __DRI_SAFETY_H__

#include "app_lift_core.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const lift_alarm_t *Dri_Safety_GetAlarm(void);
uint8_t Dri_Safety_IsEmergencyActive(void);
uint8_t Dri_Safety_IsAlarmLatched(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRI_SAFETY_H__ */

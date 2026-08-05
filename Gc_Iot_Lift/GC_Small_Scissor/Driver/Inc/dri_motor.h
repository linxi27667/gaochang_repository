/**
 * @file dri_motor.h
 * @brief Read-only driver facade for actual motor/valve output states.
 */
#ifndef __DRI_MOTOR_H__
#define __DRI_MOTOR_H__

#include "app_lift_core.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Dri_Motor_GetActual(lift_output_actual_t *out);
uint8_t Dri_Motor_IsAnyOutputOn(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRI_MOTOR_H__ */

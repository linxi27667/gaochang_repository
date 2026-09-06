#ifndef APP_RISE_QUEUE_H
#define APP_RISE_QUEUE_H

#include <stdint.h>
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

void App_RiseQueue_Init(void);
void App_RiseQueue_Enqueue(lift_role_t role,
                           uint32_t up_total,
                           uint32_t up_main,
                           uint32_t up_sub,
                           uint32_t usage_epoch);
uint32_t App_RiseQueue_GetRemainder(lift_role_t role);
void App_RiseQueue_SetRemainder(lift_role_t role, uint32_t remainder_ms);
uint8_t App_RiseQueue_ResetUsage(uint32_t usage_epoch);

#ifdef __cplusplus
}
#endif

#endif /* APP_RISE_QUEUE_H */

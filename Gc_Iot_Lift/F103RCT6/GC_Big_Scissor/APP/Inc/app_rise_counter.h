#ifndef APP_RISE_COUNTER_H
#define APP_RISE_COUNTER_H

#include <stdint.h>

#include "lift_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_RISE_COUNTER_INTERVAL_MS    3000U

/* Load the role-specific unfinished rise time after storage is ready. */
void App_RiseCounter_Init(void);

/* Call once per Lift_Task cycle after the current lift state has been updated. */
void App_RiseCounter_Poll(lift_state_t state, lift_role_t role);

/* Exposed for diagnostics and tests; the value is always below one interval. */
uint16_t App_RiseCounter_GetRemainderMs(lift_role_t role);

/* Clears unfinished rise time after a committed reset_usage command. */
void App_RiseCounter_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_RISE_COUNTER_H */

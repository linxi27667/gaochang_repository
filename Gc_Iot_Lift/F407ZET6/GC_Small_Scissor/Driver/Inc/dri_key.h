/**
 * @file dri_key.h
 * @brief Read-only driver facade for debounced lift key/input snapshots.
 */
#ifndef __DRI_KEY_H__
#define __DRI_KEY_H__

#include "app_lift_core.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const lift_input_snapshot_t *Dri_Key_GetSnapshot(void);
uint8_t Dri_Key_IsAnyMotionButtonPressed(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRI_KEY_H__ */

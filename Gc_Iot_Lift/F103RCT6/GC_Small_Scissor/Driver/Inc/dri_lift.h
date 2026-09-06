/**
 * @file dri_lift.h
 * @brief Driver-layer lift facade for scheduled 10 ms control calls.
 */
#ifndef __DRI_LIFT_H__
#define __DRI_LIFT_H__

#include "app_lift_core.h"
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

void Dri_Lift_Init(product_type_t type);
void Dri_Lift_Task10ms(void);
const lift_ctx_t *Dri_Lift_GetContext(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRI_LIFT_H__ */

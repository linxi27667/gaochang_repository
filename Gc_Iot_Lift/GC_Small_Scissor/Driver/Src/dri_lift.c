/**
 * @file dri_lift.c
 * @brief Thin driver wrapper around App_LiftCore.
 *
 * This layer owns no lift decision logic. It only exposes the APP lift core
 * through DaJian-style driver naming.
 */
#include "dri_lift.h"

#include "elog.h"
#include "main.h"

#ifndef LIFT_CORE_DEBUG
#define LIFT_CORE_DEBUG 1
#endif

#if LIFT_CORE_DEBUG == 1
#define LIFT_CORE_LOG_I(...)  elog_i(__VA_ARGS__)
#else
#define LIFT_CORE_LOG_I(...)
#endif

void Dri_Lift_Init(product_type_t type)
{
    App_LiftCore_Init(type);
    LIFT_CORE_LOG_I("LIFT", "[LIFT] core initialized product=%s", App_Product_TypeName(type));
}

void Dri_Lift_Task10ms(void)
{
    App_LiftCore_Task();
}

const lift_ctx_t *Dri_Lift_GetContext(void)
{
    return App_LiftCore_GetContext();
}

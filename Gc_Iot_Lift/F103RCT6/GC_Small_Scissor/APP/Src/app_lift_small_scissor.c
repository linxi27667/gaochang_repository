/**
 * @file app_lift_small_scissor.c
 * @brief Binds the generic lift core to PRODUCT_TYPE_SMALL_SCISSOR.
 */
#include "app_lift_small_scissor.h"

void App_LiftSmallScissor_Init(void)
{
    App_LiftCore_Init(PRODUCT_TYPE_SMALL_SCISSOR);
}

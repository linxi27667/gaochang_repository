#include "app_product.h"

product_type_t g_product_type = PRODUCT_TYPE_SMALL_SCISSOR;
lift_role_t g_current_role = LIFT_ROLE_MAIN;

const char *App_Product_TypeName(product_type_t type)
{
    return (type == PRODUCT_TYPE_SMALL_SCISSOR) ? "small_scissor" : "unknown";
}

const char *App_Product_RoleName(lift_role_t role)
{
    return (role == LIFT_ROLE_MAIN) ? "main" : "sub";
}

const char *App_Product_GetUIDString(void)
{
    return "test-uid";
}

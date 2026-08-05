#ifndef FAKE_APP_PRODUCT_H
#define FAKE_APP_PRODUCT_H

#include <stdint.h>

typedef enum {
    PRODUCT_TYPE_DOUBLE_POST = 0,
    PRODUCT_TYPE_SMALL_SCISSOR = 1,
    PRODUCT_TYPE_THIN_SCISSOR = 2,
    PRODUCT_TYPE_LARGE_SCISSOR = 3
} product_type_t;

typedef enum {
    LIFT_ROLE_MAIN = 0,
    LIFT_ROLE_SUB = 1
} lift_role_t;

extern product_type_t g_product_type;
extern lift_role_t g_current_role;

const char *App_Product_TypeName(product_type_t type);

#endif

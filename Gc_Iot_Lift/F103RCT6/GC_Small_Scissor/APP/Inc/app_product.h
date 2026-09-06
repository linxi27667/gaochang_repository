/**
 * @file app_product.h
 * @brief Product identity and STM32 UID access for the small scissor project.
 */
#ifndef __APP_PRODUCT_H__
#define __APP_PRODUCT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRODUCT_TYPE_SMALL_SCISSOR = 1,
    PRODUCT_TYPE_MAX
} product_type_t;

typedef enum {
    LIFT_ROLE_MAIN = 0,
    LIFT_ROLE_SUB = 1
} lift_role_t;

extern product_type_t g_product_type;
extern lift_role_t g_current_role;

const char *App_Product_TypeName(product_type_t type);
const char *App_Product_RoleName(lift_role_t role);
void App_Product_GetUIDWords(uint32_t uid[3]);
const char *App_Product_GetUIDString(void);

/*
 * Default hook is weak and does nothing. Override it from RTT/easylogger/USART
 * code later to route the UID lines to the selected log backend.
 */
void App_Product_UIDPrintHook(const char *line);
void App_Product_PrintUID(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_PRODUCT_H__ */

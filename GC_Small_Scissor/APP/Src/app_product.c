/**
 * @file app_product.c
 * @brief Product type, role name, and 96-bit STM32 UID print helpers.
 */
#include "app_product.h"

#include <stdio.h>
#include <string.h>
#include "elog.h"
#include "stm32f4xx_hal.h"
#include "usart.h"

#define STM32_UID_BASE_ADDR 0x1FFF7A10UL

product_type_t g_product_type = PRODUCT_TYPE_SMALL_SCISSOR;
lift_role_t g_current_role = LIFT_ROLE_MAIN;

static char s_uid_line[48];

const char *App_Product_TypeName(product_type_t type)
{
    switch (type) {
    case PRODUCT_TYPE_SMALL_SCISSOR:
        return "small_scissor";
    default:
        return "unknown";
    }
}

const char *App_Product_RoleName(lift_role_t role)
{
    return (role == LIFT_ROLE_MAIN) ? "main" : "sub";
}

void App_Product_GetUIDWords(uint32_t uid[3])
{
    const volatile uint32_t *uid_base = (const volatile uint32_t *)STM32_UID_BASE_ADDR;

    if (uid == NULL) {
        return;
    }

    uid[0] = uid_base[0];
    uid[1] = uid_base[1];
    uid[2] = uid_base[2];
}

const char *App_Product_GetUIDString(void)
{
    uint32_t uid[3];

    App_Product_GetUIDWords(uid);
    (void)snprintf(s_uid_line, sizeof(s_uid_line),
                   "%08lX-%08lX-%08lX",
                   (unsigned long)uid[0],
                   (unsigned long)uid[1],
                   (unsigned long)uid[2]);

    return s_uid_line;
}

__weak void App_Product_UIDPrintHook(const char *line)
{
    if (line == NULL) {
        return;
    }

    elog_i("UID", "%s", line);
}

void App_Product_PrintUID(void)
{
    uint32_t uid[3];
    char line[96];

    App_Product_GetUIDWords(uid);

    (void)snprintf(line, sizeof(line),
                   "[UID] product=%s role=%s chip_unique_id=0x%08lX-%08lX-%08lX",
                   App_Product_TypeName(g_product_type),
                   App_Product_RoleName(g_current_role),
                   (unsigned long)uid[0],
                   (unsigned long)uid[1],
                   (unsigned long)uid[2]);
    App_Product_UIDPrintHook(line);

    (void)snprintf(line, sizeof(line),
                   "[UID] bytes=%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX",
                   (unsigned long)((uid[0] >> 0) & 0xFFU),
                   (unsigned long)((uid[0] >> 8) & 0xFFU),
                   (unsigned long)((uid[0] >> 16) & 0xFFU),
                   (unsigned long)((uid[0] >> 24) & 0xFFU),
                   (unsigned long)((uid[1] >> 0) & 0xFFU),
                   (unsigned long)((uid[1] >> 8) & 0xFFU),
                   (unsigned long)((uid[1] >> 16) & 0xFFU),
                   (unsigned long)((uid[1] >> 24) & 0xFFU),
                   (unsigned long)((uid[2] >> 0) & 0xFFU),
                   (unsigned long)((uid[2] >> 8) & 0xFFU),
                   (unsigned long)((uid[2] >> 16) & 0xFFU),
                   (unsigned long)((uid[2] >> 24) & 0xFFU));
    App_Product_UIDPrintHook(line);
}

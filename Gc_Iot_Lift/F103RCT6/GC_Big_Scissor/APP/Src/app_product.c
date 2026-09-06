#include "app_product.h"
#include "elog.h"

/* 全局对象定义 */
product_type_t g_product_type = PRODUCT_TYPE_LARGE_SCISSOR;
lift_role_t    g_current_role = LIFT_ROLE_MAIN;

const char *App_Product_TypeName(product_type_t type)
{
    switch (type) {
        case PRODUCT_TYPE_DOUBLE_POST:   return "double_post";
        case PRODUCT_TYPE_SMALL_SCISSOR: return "small_scissor";
        case PRODUCT_TYPE_THIN_SCISSOR:  return "thin_scissor";
        case PRODUCT_TYPE_LARGE_SCISSOR: return "large_scissor";
        default:                         return "unknown";
    }
}

const char *App_Product_RoleName(lift_role_t role)
{
    return (role == LIFT_ROLE_MAIN) ? "main" : "sub";
}

/* ============ 芯片出厂 UID 读取 ============ */
/* STM32F103 出厂 UID 寄存器（96 位 / 12 字节，3 个 32 位字）
 * 地址 0x1FFF7A10: U_ID[31:0]
 * 地址 0x1FFF7A14: U_ID[63:32]
 * 地址 0x1FFF7A18: U_ID[95:64]
 */
#define STM32_UID_BASE_ADDR  0x1FFFF7E8U

void App_Product_PrintUID(void)
{
    uint32_t *uid_base = (uint32_t *)STM32_UID_BASE_ADDR;
    uint32_t w0 = uid_base[0];
    uint32_t w1 = uid_base[1];
    uint32_t w2 = uid_base[2];

    /* 一行十六进制（便于扫码枪/手工录入），一行可读分字节格式 */
    elog_i("UID", "[UID] chip_unique_id=0x%08lX-%08lX-%08lX", w0, w1, w2);
    elog_i("UID", "[UID] bytes: %02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX-%02lX",
           (w0 >> 0)  & 0xFF, (w0 >> 8)  & 0xFF, (w0 >> 16) & 0xFF, (w0 >> 24) & 0xFF,
           (w1 >> 0)  & 0xFF, (w1 >> 8)  & 0xFF, (w1 >> 16) & 0xFF, (w1 >> 24) & 0xFF,
           (w2 >> 0)  & 0xFF, (w2 >> 8)  & 0xFF, (w2 >> 16) & 0xFF, (w2 >> 24) & 0xFF);
}

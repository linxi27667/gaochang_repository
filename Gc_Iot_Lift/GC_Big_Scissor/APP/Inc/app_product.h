#ifndef __APP_PRODUCT_H__
#define __APP_PRODUCT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 产品型号枚举 ============ */
typedef enum {
    PRODUCT_TYPE_DOUBLE_POST    = 0,   /* 双柱举升机 */
    PRODUCT_TYPE_SMALL_SCISSOR  = 1,   /* 小剪举升机 */
    PRODUCT_TYPE_THIN_SCISSOR   = 2,   /* 超薄小剪举升机 */
    PRODUCT_TYPE_LARGE_SCISSOR  = 3,   /* 大剪举升机 */
    PRODUCT_TYPE_MAX
} product_type_t;

/* ============ 大剪主/子机角色 ============ */
typedef enum {
    LIFT_ROLE_MAIN = 0,   /* 主机 */
    LIFT_ROLE_SUB  = 1    /* 子机 */
} lift_role_t;

/* ============ 全局对象 ============ */
/* 当前产品类型（从 g_config.product_type 加载） */
extern product_type_t g_product_type;
/* 大剪当前角色（旋转开关实时读取） */
extern lift_role_t g_current_role;

/* ============ 接口 ============ */
const char *App_Product_TypeName(product_type_t type);
const char *App_Product_RoleName(lift_role_t role);

/* 打印芯片出厂 UID（96 位），供工人用 J-Link RTT 读取进行设备绑定 */
void App_Product_PrintUID(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_PRODUCT_H__ */

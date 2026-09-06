#ifndef __LIFT_CORE_H__
#define __LIFT_CORE_H__

#include <stdint.h>
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============ 举升机运行状态 ============ */
typedef enum {
    LIFT_STATE_IDLE        = 0,   /* 空闲 */
    LIFT_STATE_RISING      = 1,   /* 上升中 */
    LIFT_STATE_DROPPING    = 2,   /* 下降中 */
    LIFT_STATE_LOCKED      = 3,   /* 锁定保持 */
    LIFT_STATE_REFILLING   = 4,   /* 补油中 */
    LIFT_STATE_ESTOP       = 5,   /* 急停 */
    LIFT_STATE_PHOTO_ALARM = 6    /* 光电报警，需远程解除 */
} lift_state_t;

/* ============ 产品操作接口（每个型号实现一组） ============ */
typedef struct {
    void (*init)(void);
    void (*on_up_pressed)(void);            /* 上升按钮按下 */
    void (*on_down_pressed)(void);          /* 下降按钮按下 */
    void (*on_lock_pressed)(void);          /* 锁定按钮按下 */
    void (*on_refill_pressed)(void);        /* 上升+补油同时按下 */
    void (*on_estop)(void);                 /* 急停触发 */
    void (*on_photoelectric_blocked)(void); /* 光电遮挡 */
    void (*on_clear_alarm)(void);           /* 远程解除报警 */
    void (*on_remote_lock)(void);           /* 远程锁定，清除产品内部动作状态 */
    void (*poll)(void);                     /* 周期调用，处理时序状态机 */
} lift_ops_t;

/* ============ 全局对象 ============ */
extern volatile lift_state_t g_lift_state;
extern const lift_ops_t *g_lift_ops;
extern volatile uint8_t s_remote_locked;   /* 远程锁定标志（IoT 命令设置），lock.c 持锁读写 */

/* ============ 接口 ============ */

/**
 * @brief  控制框架初始化（根据 g_product_type 选择 ops）
 */
void LiftCore_Init(void);

/**
 * @brief  控制框架周期调用（由 Lift_Task 调用）
 *         处理急停/光电全局保护，然后分发到产品 ops->poll()
 */
void LiftCore_Poll(void);

/**
 * @brief  远程解除报警（由 IoT 命令处理调用）
 */
void LiftCore_ClearAlarm(void);

/**
 * @brief  远程锁定/解锁
 */
void LiftCore_SetRemoteLock(uint8_t locked);

/**
 * @brief  禁止重新动作，直到所有本地动作按钮均已释放
 */
void LiftCore_RequireMotionRearm(void);

/**
 * @brief  状态名转字符串
 */
const char *LiftCore_StateName(lift_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* __LIFT_CORE_H__ */

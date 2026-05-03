#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>
#include "main.h"

/* ==================== 安全状态 ==================== */
typedef struct {
    volatile uint8_t  collision_triggered;          /* 防碰杆触发 */
    uint8_t           stall_detected;               /* 堵转检测 */
    uint8_t           balance_timeout;              /* 平衡超时 */
    uint8_t           secondary_descent_triggered;  /* 二次下降触发 */
    uint8_t           secondary_descent_confirmed;  /* 二次下降确认 */
    volatile uint8_t  at_lower_limit;               /* 已到下限位 */
    volatile uint8_t  alarm_state;                  /* 报警状态 */
    volatile uint32_t last_pulse_tick[2];           /* 每个立柱最后脉冲时刻 */
} safety_state_t;

/* ==================== 报警码 ==================== */
#define ALARM_NONE            0
#define ALARM_COLLISION       1
#define ALARM_STALL           2
#define ALARM_BALANCE_TIMEOUT 3
#define ALARM_UPPER_LIMIT     4

/* ==================== 全局变量 ==================== */
extern safety_state_t g_safety;

/* ==================== 安全输入引脚宏（用户可按实际接线修改）==================== */

#define LOWER_LIMIT_PORT    GPIOB
#define LOWER_LIMIT_PIN     GPIO_PIN_0    /* 下限位，EXTI rising */

#define COLLISION_1_PORT    GPIOB
#define COLLISION_1_PIN     GPIO_PIN_1    /* 1# 防碰杆，EXTI falling */

#define COLLISION_2_PORT    GPIOB
#define COLLISION_2_PIN     GPIO_PIN_2    /* 2# 防碰杆，EXTI falling */

/* ==================== API ==================== */

void Safety_Init(void);
void Safety_EXTI_Handler(uint16_t gpio_pin);
void Safety_Check_Stall(void);
void Safety_Check_Secondary_Descent(void);
void Safety_Check_Upper_Limit(void);
void Safety_Check_Lower_Limit(void);
void Safety_Alarm_Reset(void);

#endif

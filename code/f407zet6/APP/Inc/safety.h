#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>
#include "main.h"

typedef enum {
    ALARM_NONE            = 0,   // 无报警状态
    ALARM_COLLISION       = 1,   // 碰撞报警（检测到碰撞）
    ALARM_STALL           = 2,   // 失速报警（电机堵转）
    ALARM_BALANCE_TIMEOUT = 3,   // 平衡超时报警（平衡控制超时）
} alarm_t;                       // 报警类型枚举

typedef struct {
    volatile alarm_t  alarm;
    uint8_t           secondary_descent_triggered;
    uint8_t           secondary_descent_confirmed;
    volatile uint8_t  at_lower_limit;
    volatile uint8_t  at_upper_limit;
    volatile uint8_t  stall_suspected;
    volatile uint32_t last_pulse_tick[2];
} safety_state_t;

extern safety_state_t g_safety;

void Safety_Init(void);
void Safety_EXTI_Handler(uint16_t gpio_pin);
void Safety_Check_Stall(void);
void Safety_Check_Secondary_Descent(void);
void Safety_Check_Upper_Limit(void);
void Safety_Check_Lower_Limit(void);
void Safety_Alarm_Reset(void);
uint8_t Safety_Alarm_Handle(void);
void    Safety_Running_Update(void);

#endif

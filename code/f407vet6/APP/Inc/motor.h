#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>
#include "main.h"

/* ==================== 枚举 ==================== */
typedef enum {
    MOTOR_STOPPED         = 0,   /* 停止 */
    MOTOR_RUNNING         = 1,   /* 运行中 */
    MOTOR_WAITING_BALANCE = 2,   /* 等待另一柱同步 */
} motor_state_t;

typedef enum {
    DIR_STOP = 0,   /* 无运动 */
    DIR_UP   = 1,   /* 上升 */
    DIR_DOWN = 2,   /* 下降 */
} direction_t;

/* ==================== 立柱状态 ==================== */
typedef struct {
    volatile int32_t pulse_count;
    uint32_t         last_pulse_tick;
    uint8_t          counting_enable;
    motor_state_t    motor_state;
    uint32_t         wait_start_tick;
} motor_column_t;

/* ==================== 全局变量 ==================== */
extern motor_column_t g_column[2];

/* ==================== 引脚宏 ==================== */

#define COL_LEFT_MAIN_PORT   GPIOC
#define COL_LEFT_MAIN_PIN    GPIO_PIN_0    /* 左柱主接触器 */

#define COL_RIGHT_MAIN_PORT  GPIOC
#define COL_RIGHT_MAIN_PIN   GPIO_PIN_4    /* 右柱主接触器 */

#define RELAY_UP_PORT        GPIOC
#define RELAY_UP_PIN         GPIO_PIN_2    /* 上升继电器（共享） */

#define RELAY_DOWN_PORT      GPIOC
#define RELAY_DOWN_PIN       GPIO_PIN_5    /* 下降继电器（共享） */

/* 继电器驱动电平：低电平吸合 */
#define RELAY_ON         GPIO_PIN_RESET
#define RELAY_OFF        GPIO_PIN_SET

/* 总开关宏（按列索引查引脚） */
#define COL_MAIN_PORT(col)  ((col) == 0 ? COL_LEFT_MAIN_PORT  : COL_RIGHT_MAIN_PORT)
#define COL_MAIN_PIN(col)   ((col) == 0 ? COL_LEFT_MAIN_PIN   : COL_RIGHT_MAIN_PIN)

/* ==================== API ==================== */

void Motor_Init(void);
void Motor_Start(uint8_t column_index, direction_t direction);
void Motor_Stop(uint8_t column_index);
void Motor_Pause(uint8_t column_index);   /* 平衡用：只停单柱，保留方向 */
void Motor_Stop_All(void);
void Motor_Start_All(direction_t direction);

/* ISR 安全版本：无延时，可直接在中断中调用 */
void Motor_Stop_All_Immediate(void);

#endif

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

/* ==================== 引脚宏（用户可按实际接线修改）==================== */

#define MOTOR1_UP_PORT   GPIOC
#define MOTOR1_UP_PIN    GPIO_PIN_0

#define MOTOR2_UP_PORT   GPIOC
#define MOTOR2_UP_PIN    GPIO_PIN_1

#define MOTOR1_MAIN_PORT GPIOC
#define MOTOR1_MAIN_PIN  GPIO_PIN_2

#define MOTOR2_MAIN_PORT GPIOC
#define MOTOR2_MAIN_PIN  GPIO_PIN_3

#define BRAKE_PORT       GPIOC
#define BRAKE_PIN        GPIO_PIN_4

#define REVERSE_PORT     GPIOC
#define REVERSE_PIN      GPIO_PIN_5

/* 继电器驱动电平：低电平吸合 */
#define RELAY_ON         GPIO_PIN_RESET
#define RELAY_OFF        GPIO_PIN_SET
#define BRAKE_RELEASE    GPIO_PIN_RESET  /* 通电释放刹车 */
#define BRAKE_HOLD       GPIO_PIN_SET    /* 断电抱紧 */

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

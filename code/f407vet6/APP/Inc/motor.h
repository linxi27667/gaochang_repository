#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>
#include "main.h"

/* ==================== 立柱状态 ==================== */
typedef struct {
    volatile int32_t pulse_count;     /* 脉冲累计（ISR 更新） */
    uint32_t         last_pulse_tick; /* 最后收到脉冲的时刻 */
    uint8_t          counting_enable; /* 计数使能 */
    uint8_t          motor_state;     /* 电机状态 */
    uint32_t         wait_start_tick; /* 等待开始的时刻 */
} motor_column_t;

/* ==================== 电机状态 ==================== */
#define MOTOR_STOPPED          0
#define MOTOR_RUNNING          1
#define MOTOR_WAITING_BALANCE  2
#define MOTOR_BLOCKED          3

/* ==================== 方向 ==================== */
#define DIR_STOP  0
#define DIR_UP    1
#define DIR_DOWN  2

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

/* 继电器驱动电平：高电平吸合 */
#define RELAY_ON         GPIO_PIN_SET
#define RELAY_OFF        GPIO_PIN_RESET
#define BRAKE_RELEASE    GPIO_PIN_SET    /* 通电释放刹车 */
#define BRAKE_HOLD       GPIO_PIN_RESET  /* 断电抱紧 */

/* ==================== API ==================== */

void Motor_Init(void);
void Motor_Start(uint8_t column_index, uint8_t direction);
void Motor_Stop(uint8_t column_index);
void Motor_Stop_All(void);
void Motor_Start_All(uint8_t direction);

/* ISR 安全版本：无延时，可直接在中断中调用 */
void Motor_Stop_All_Immediate(void);

#endif

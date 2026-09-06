#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>
#include "main.h"

typedef enum {
    MOTOR_STOPPED         = 0,
    MOTOR_RUNNING         = 1,
    MOTOR_WAITING_BALANCE = 2,
} motor_state_t;

typedef enum {
    DIR_STOP = 0,
    DIR_UP   = 1,
    DIR_DOWN = 2,
} direction_t;

typedef struct {
    volatile int32_t pulse_count;
    uint32_t         last_pulse_tick;
    uint8_t          counting_enable;
    motor_state_t    motor_state;
    uint32_t         wait_start_tick;
} motor_column_t;

extern motor_column_t g_column[2];

/* ===== 继电器引脚（F103RCT6）===== */
/* PB12 左电机电源 RELAY0 */
#define COL_LEFT_MAIN_PORT   RELAY0_GPIO_Port
#define COL_LEFT_MAIN_PIN    RELAY0_Pin

/* PB13 右电机电源 RELAY1 */
#define COL_RIGHT_MAIN_PORT  RELAY1_GPIO_Port
#define COL_RIGHT_MAIN_PIN   RELAY1_Pin

/* PB14 上升 RELAY2 */
#define RELAY_UP_PORT        RELAY2_GPIO_Port
#define RELAY_UP_PIN         RELAY2_Pin

/* PB15 下降 RELAY3 */
#define RELAY_DOWN_PORT      RELAY3_GPIO_Port
#define RELAY_DOWN_PIN       RELAY3_Pin

/* 继电器驱动电平：高电平吸合 */
#define RELAY_ON         GPIO_PIN_SET
#define RELAY_OFF        GPIO_PIN_RESET

#define COL_MAIN_PORT(col)  ((col) == 0 ? COL_LEFT_MAIN_PORT  : COL_RIGHT_MAIN_PORT)
#define COL_MAIN_PIN(col)   ((col) == 0 ? COL_LEFT_MAIN_PIN   : COL_RIGHT_MAIN_PIN)

/* 丝杆导程：1脉冲 = 8mm */
#define SCREW_LEAD_MM       8

void Motor_Init(void);
void Motor_Start(uint8_t column_index, direction_t direction);
void Motor_Stop(uint8_t column_index);
void Motor_Pause(uint8_t column_index);
void Motor_Stop_All(void);
void Motor_Start_All(direction_t direction);
void Motor_Stop_All_Immediate(void);
uint8_t Motor_Admin_Jog(uint8_t column_index, direction_t direction, uint32_t duration_ms);

#endif

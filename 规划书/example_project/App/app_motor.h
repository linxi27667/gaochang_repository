/*
 * app_motor.h - 电机控制任务声明
 */
#ifndef APP_MOTOR_H
#define APP_MOTOR_H

#include <stdint.h>

void Motor_Task(void *arg);

/* 电机方向枚举 */
typedef enum {
    DIR_STOP = 0,
    DIR_RISE = 1,
    DIR_FALL = 2
} MotorDir_t;

/* 系统状态枚举 */
typedef enum {
    STATE_IDLE = 0,
    STATE_RISING,
    STATE_FALLING,
    STATE_SYNC_WAIT,
    STATE_ALARM,
    STATE_EMERGENCY_STOP,
    STATE_WAITING_SECONDARY_CONFIRM,
} SystemState_t;

extern volatile SystemState_t system_state;

#endif /* APP_MOTOR_H */

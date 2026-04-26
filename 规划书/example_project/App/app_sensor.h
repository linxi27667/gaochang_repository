/*
 * app_sensor.h - 传感器任务声明 + 数据结构
 */
#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"

/* 单柱状态（对称对象用数组） */
typedef struct {
    int32_t  total_pulses;        /* C8/C9 计圈数 */
    uint32_t last_pulse_tick;     /* 堵转检测：上次收到脉冲的 tick */
    uint8_t  motor_running;       /* 电机运行标志 */
    uint8_t  direction;           /* 0=停 1=上升 2=下降 */
    uint8_t  sync_blocked;        /* 同步锁定标志 */
    uint8_t  alarm_flag;          /* 报警标志 */
} Column_t;

/* 全局共享资源 */
extern Column_t cols[2];                   /* cols[0]=柱1, cols[1]=柱2 */
extern SemaphoreHandle_t hColsMutex;       /* 保护 cols[] 的互斥量 */

void Sensor_Task(void *arg);

#endif /* APP_SENSOR_H */

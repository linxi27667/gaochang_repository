/*
 * app_alarm.h - 报警任务声明
 */
#ifndef APP_ALARM_H
#define APP_ALARM_H

#include <stdint.h>

/* 报警码 */
typedef enum {
    ALARM_NONE = 0,
    ALARM_STALL_COL1,       /* 柱1 堵转 */
    ALARM_STALL_COL2,       /* 柱2 堵转 */
} AlarmCode_t;

void Alarm_Task(void *arg);

/* 堵转超时：500ms（可根据实测调整） */
#define STALL_TIMEOUT_MS    500

#endif /* APP_ALARM_H */

/*
 * app_alarm.c - 报警检测任务
 *
 * 100ms 周期：堵转检测 + 限位报警
 * 对应原 PLC "报警程序-霍尔故障段"
 */
#include "app_alarm.h"
#include "app_sensor.h"
#include "app_motor.h"
#include "drv_gpio.h"
#include "FreeRTOS.h"
#include "task.h"

static AlarmCode_t current_alarm = ALARM_NONE;

/* ================= 堵转检测 ================= */
static void Check_Stall_Alarm(void) {
    uint32_t now = xTaskGetTickCount();

    xSemaphoreTake(hColsMutex, portMAX_DELAY);

    /* 柱1：电机运行中，超过阈值时间没收到脉冲 → 堵转 */
    if (cols[0].motor_running && !cols[0].sync_blocked) {
        if ((now - cols[0].last_pulse_tick) > pdMS_TO_TICKS(STALL_TIMEOUT_MS)) {
            current_alarm = ALARM_STALL_COL1;
            cols[0].alarm_flag = 1;
        }
    }

    /* 柱2：同上 */
    if (cols[1].motor_running && !cols[1].sync_blocked) {
        if ((now - cols[1].last_pulse_tick) > pdMS_TO_TICKS(STALL_TIMEOUT_MS)) {
            current_alarm = ALARM_STALL_COL2;
            cols[1].alarm_flag = 1;
        }
    }

    xSemaphoreGive(hColsMutex);
}

/* ================= 任务入口 ================= */
void Alarm_Task(void *arg) {
    while (1) {
        Check_Stall_Alarm();
        vTaskDelay(pdMS_TO_TICKS(100));   /* 100ms 周期 */
    }
}

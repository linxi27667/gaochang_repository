/*
 * app_sensor.c - 传感器读取任务
 *
 * 工作方式：
 *   1. 阻塞等待 TIM5 中断通知（霍尔脉冲）→ 计圈
 *   2. 轮询检测下限位 → 到下限清零计数
 */
#include "app_sensor.h"
#include "drv_gpio.h"
#include "FreeRTOS.h"
#include "task.h"

/* ================= 全局数据 ================= */
Column_t cols[2] = {0};
SemaphoreHandle_t hColsMutex = NULL;

/* ================= 任务句柄（中断需要用到） ================= */
TaskHandle_t hSensorTask = NULL;

/* ================= 限位检测（下限位触发 → RST 计数清零） ================= */
static void Check_Lower_Limits(void) {
    xSemaphoreTake(hColsMutex, portMAX_DELAY);

    if (LOWER_LIM_1_Read() == GPIO_PIN_SET) {
        cols[0].total_pulses = 0;
        cols[0].direction = 0;
    }
    if (LOWER_LIM_2_Read() == GPIO_PIN_SET) {
        cols[1].total_pulses = 0;
        cols[1].direction = 0;
    }

    xSemaphoreGive(hColsMutex);
}

/* ================= 任务入口 ================= */
void Sensor_Task(void *arg) {
    /* 注册自己的句柄，供 drv_tim.c 中断回调使用 */
    hSensorTask = xTaskGetCurrentTaskHandle();

    while (1) {
        /* 阻塞等待 TIM5 中断通知，超时 100ms 也唤醒去检查限位 */
        uint32_t value = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        xSemaphoreTake(hColsMutex, portMAX_DELAY);
        if (value == 1) {
            cols[0].total_pulses++;
            cols[0].last_pulse_tick = xTaskGetTickCount();
        } else if (value == 2) {
            cols[1].total_pulses++;
            cols[1].last_pulse_tick = xTaskGetTickCount();
        }
        xSemaphoreGive(hColsMutex);

        /* 每次唤醒都检查下限位 */
        Check_Lower_Limits();
    }
}

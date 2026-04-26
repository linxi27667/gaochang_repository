/*
 * app_motor.c - 电机控制任务
 *
 * 10ms 周期：读取按钮 → 状态机 → 更新电机输出
 * 业务层通过 drv_gpio.h 的宏读写引脚，不直接调 HAL
 */
#include "app_motor.h"
#include "drv_gpio.h"
#include "app_sensor.h"       /* cols[] 数组声明 */
#include "FreeRTOS.h"
#include "task.h"

volatile SystemState_t system_state = STATE_IDLE;

/* ================= 电机输出更新 ================= */
static void Motor_Update_Outputs(void) {
    xSemaphoreTake(hColsMutex, portMAX_DELAY);

    for (int i = 0; i < 2; i++) {
        if (cols[i].motor_running && !cols[i].sync_blocked && !cols[i].alarm_flag) {
            if (cols[i].direction == DIR_RISE) {
                if (i == 0) { KM1_RISE_ON();  KM1_FALL_OFF(); }
                else        { KM2_RISE_ON();  KM2_FALL_OFF(); }
            } else if (cols[i].direction == DIR_FALL) {
                if (i == 0) { KM1_RISE_OFF(); KM1_FALL_ON();  }
                else        { KM2_RISE_OFF(); KM2_FALL_ON();  }
            }
        } else {
            /* 停机：正反转都断开 */
            if (i == 0) { KM1_RISE_OFF(); KM1_FALL_OFF(); }
            else        { KM2_RISE_OFF(); KM2_FALL_OFF(); }
        }
    }

    xSemaphoreGive(hColsMutex);
}

/* ================= 状态机处理 ================= */
static void State_Machine_Process(void) {
    switch (system_state) {
        case STATE_IDLE:
            if (KEY_RISE_Read() == GPIO_PIN_SET) {
                system_state = STATE_RISING;
                cols[0].motor_running = 1;
                cols[1].motor_running = 1;
                cols[0].direction = DIR_RISE;
                cols[1].direction = DIR_RISE;
            } else if (KEY_FALL_Read() == GPIO_PIN_SET) {
                system_state = STATE_FALLING;
                cols[0].motor_running = 1;
                cols[1].motor_running = 1;
                cols[0].direction = DIR_FALL;
                cols[1].direction = DIR_FALL;
            }
            break;

        case STATE_RISING:
            /* 松按钮 → 停止 */
            if (KEY_RISE_Read() == GPIO_PIN_RESET) {
                system_state = STATE_IDLE;
                cols[0].motor_running = 0;
                cols[1].motor_running = 0;
                cols[0].direction = DIR_STOP;
                cols[1].direction = DIR_STOP;
            }
            /* 上限位 → 该柱停 */
            if (UPPER_LIM_1_Read() == GPIO_PIN_RESET) {
                cols[0].motor_running = 0;
                cols[0].direction = DIR_STOP;
            }
            if (UPPER_LIM_2_Read() == GPIO_PIN_RESET) {
                cols[1].motor_running = 0;
                cols[1].direction = DIR_STOP;
            }
            break;

        case STATE_FALLING:
            if (KEY_FALL_Read() == GPIO_PIN_RESET) {
                system_state = STATE_IDLE;
                cols[0].motor_running = 0;
                cols[1].motor_running = 0;
                cols[0].direction = DIR_STOP;
                cols[1].direction = DIR_STOP;
            }
            break;

        default:
            break;
    }
}

/* ================= 任务入口 ================= */
void Motor_Task(void *arg) {
    while (1) {
        State_Machine_Process();
        Motor_Update_Outputs();
        vTaskDelay(pdMS_TO_TICKS(10));   /* 10ms 周期 */
    }
}

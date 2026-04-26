/*
 * app_comm.c - HMI 通信任务（骨架）
 *
 * 周期性上报：柱1脉冲 + 柱2脉冲 + 差值 + 状态 + 报警
 */
#include "app_comm.h"
#include "app_sensor.h"
#include "app_motor.h"
#include "app_alarm.h"
#include "FreeRTOS.h"
#include "task.h"

/* USART1 句柄（由 drv_usart.c 初始化） */
extern UART_HandleTypeDef huart1;

void Comm_Task(void *arg) {
    while (1) {
        xSemaphoreTake(hColsMutex, portMAX_DELAY);

        int32_t col1_pulses = cols[0].total_pulses;
        int32_t col2_pulses = cols[1].total_pulses;
        int32_t diff = col1_pulses - col2_pulses;

        xSemaphoreGive(hColsMutex);

        /*
         * 实际项目中这里发送 HMI 协议帧：
         * [0xAA][长度][0x02][col1_hi][col1_lo][col2_hi][col2_lo][diff][state][alarm][CRC8][0x55]
         *
         * HAL_UART_Transmit(&huart1, tx_buf, len, 100);
         */

        vTaskDelay(pdMS_TO_TICKS(200));   /* 200ms 上报周期 */
    }
}

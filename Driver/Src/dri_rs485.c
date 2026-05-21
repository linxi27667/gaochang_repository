#include "dri_rs485.h"
#include "app_rs485.h"
#include "cmsis_os.h"
#include <string.h>

/* ==================== 任务创建 ==================== */

void RS485_Task_Create(void)
{
    xTaskCreate(RS485_Task, "rs485_echo", 256, NULL, tskIDLE_PRIORITY + 2, NULL);
}

/* ==================== RS485回显测试任务 ==================== */

void RS485_Task(void *pvParameters)
{
    (void)pvParameters;
    uint8_t buf[128];
    uint32_t last_test_tick = 0;

    while (1) {
        /* 每2秒发送一次测试消息 */
        uint32_t now = HAL_GetTick();
        if (now - last_test_tick > 2000) {
            last_test_tick = now;
            char *test_msg = "RS485 self-test OK\r\n";
            RS485_Send((uint8_t *)test_msg, strlen(test_msg));
        }

        /* 处理接收 */
        uint16_t count = RS485_Available();
        if (count > 0) {
            uint16_t len = RS485_Recv(buf, sizeof(buf));
            if (len > 0) {
                RS485_Send(buf, len);
            }
        }
        osDelay(10);
    }
}

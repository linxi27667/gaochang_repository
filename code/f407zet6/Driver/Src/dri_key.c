#include "dri_key.h"
#include "key.h"
#include "cmsis_os.h"
#include "elog.h"

void Key_Task_Create(void)
{
    xTaskCreate(Key_Task, "key", 512, NULL, tskIDLE_PRIORITY + 2, NULL);
}

/* 按键扫描任务 — 每20ms调用一次 Key_Scan() 更新按键状态
 * 每5秒打印一次心跳(250次×20ms=5000ms)，确认任务未卡死
 * 按键按下/松手的即时日志在 key.c 的状态机中打印 */
void Key_Task(void *pvParameters)
{
    (void)pvParameters;
    static uint32_t hb_cnt = 0;

    while (1)
    {
        Key_Scan();

        hb_cnt++;
        if (hb_cnt >= 250) {
            hb_cnt = 0;
            const char *state = "IDLE";
            if (g_key[0].f_hold)      state = "UP";
            else if (g_key[1].f_hold) state = "DOWN";
            elog_i("KEY", "key_state %s", state);
        }

        osDelay(20);
    }
}

#include "dri_key.h"
#include "key.h"
#include "cmsis_os.h"

/* ==================== 按键任务创建 ==================== */

void Key_Task_Create(void)
{
    xTaskCreate(Key_Task, "key", 512, NULL, tskIDLE_PRIORITY + 2, NULL);
}

/* ==================== 按键任务（50ms 周期） ==================== */

void Key_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        Key_Scan();
        Buzzer_Poll();

        osDelay(50);
    }
}

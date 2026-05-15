#include "dri_key.h"
#include "key.h"
#include "cmsis_os.h"
#include "elog.h"

void Key_Task_Create(void)
{
    xTaskCreate(Key_Task, "key", 512, NULL, tskIDLE_PRIORITY + 2, NULL);
}

void Key_Task(void *pvParameters)
{
    (void)pvParameters;
    static uint32_t hb_cnt = 0;

    while (1)
    {
        Key_Scan();

        hb_cnt++;
        if (hb_cnt >= 50) {
            hb_cnt = 0;
            elog_i("KEY", "HB up=%d down=%d stop=%d",
                   g_key[0].f_hold, g_key[1].f_hold, g_key[2].f_push);
        }

        osDelay(20);
    }
}

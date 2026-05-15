#include "dri_safety.h"
#include "safety.h"
#include "motor.h"
#include "key.h"
#include "cmsis_os.h"
#include "elog.h"

void Safety_Task_Create(void)
{
    xTaskCreate(Safety_Task, "safety", 256, NULL, tskIDLE_PRIORITY + 3, NULL);
}

void Safety_Task(void *pvParameters)
{
    (void)pvParameters;
    static uint32_t hb_cnt = 0;

    while (1)
    {
        Safety_Check_Stall();

        if ((g_safety.alarm != ALARM_NONE || g_safety.stall_suspected)
            && g_command.button_stop) {
            Safety_Alarm_Reset();
        }

        hb_cnt++;
        if (hb_cnt >= 100) {
            hb_cnt = 0;
            elog_i("SAFETY", "HB alarm=%d stall=%d upper=%d lower=%d",
                   g_safety.alarm, g_safety.stall_suspected,
                   g_safety.at_upper_limit, g_safety.at_lower_limit);
        }

        osDelay(10);
    }
}

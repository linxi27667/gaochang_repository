#include "dri_safety.h"
#include "safety.h"
#include "cmsis_os.h"
#include "elog.h"

void Safety_Task_Create(void)
{
    xTaskCreate(Safety_Task, "safety", 256, NULL, tskIDLE_PRIORITY + 3, NULL);
}

void Safety_Task(void *pvParameters)
{
    (void)pvParameters;
    alarm_t last_alarm = ALARM_NONE;
    uint8_t last_stall = 0;
    uint8_t last_upper = 0;
    uint8_t last_lower = 0;

    while (1)
    {
        Safety_Check_Stall();

        if (last_alarm != g_safety.alarm ||
            last_stall != g_safety.stall_suspected ||
            last_upper != g_safety.at_upper_limit ||
            last_lower != g_safety.at_lower_limit) {
            last_alarm = g_safety.alarm;
            last_stall = g_safety.stall_suspected;
            last_upper = g_safety.at_upper_limit;
            last_lower = g_safety.at_lower_limit;
            elog_i("SAFETY", "[SAFETY] state alarm=%d stall=%d upper=%d lower=%d",
                   last_alarm, last_stall, last_upper, last_lower);
        }

        osDelay(10);
    }
}

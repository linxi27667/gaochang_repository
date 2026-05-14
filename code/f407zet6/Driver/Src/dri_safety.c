#include "dri_safety.h"
#include "safety.h"
#include "motor.h"
#include "key.h"
#include "cmsis_os.h"

/* ==================== 安全任务创建 ==================== */

void Safety_Task_Create(void)
{
    xTaskCreate(Safety_Task, "safety", 256, NULL, tskIDLE_PRIORITY + 3, NULL);
}

/* ==================== 安全任务（10ms 周期） ==================== */

void Safety_Task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        Safety_Check_Stall();

        /* 二次下降确认键 */
        if (g_command.button_confirm && g_safety.secondary_descent_triggered) {
            g_safety.secondary_descent_confirmed = 1;
        }

        /* 报警状态下按停止键 → 复位报警 */
        if ((g_safety.alarm != ALARM_NONE || g_safety.stall_suspected)
            && g_command.button_stop) {
            Safety_Alarm_Reset();
        }

        osDelay(10);
    }
}

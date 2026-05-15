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

/* 安全监控任务 — 10ms周期
 * 1. 堵转检测：运行中2秒无脉冲 → 告警停机
 * 2. 告警复位：双键同按(上升+下降) → 清除告警
 * 3. 心跳日志：每5秒(500次×10ms=5000ms) */
void Safety_Task(void *pvParameters)
{
    (void)pvParameters;
    static uint32_t hb_cnt = 0;

    while (1)
    {
        Safety_Check_Stall();

        /* 双键同按复位告警 */
        if ((g_safety.alarm != ALARM_NONE || g_safety.stall_suspected)
            && g_command.button_up && g_command.button_down) {
            Safety_Alarm_Reset();
        }

        hb_cnt++;
        if (hb_cnt >= 500) {
            hb_cnt = 0;
            elog_i("SAFETY", "heartbeat alarm=%d stall=%d upper_limit=%d lower_limit=%d",
                   g_safety.alarm, g_safety.stall_suspected,
                   g_safety.at_upper_limit, g_safety.at_lower_limit);
        }

        osDelay(10);
    }
}

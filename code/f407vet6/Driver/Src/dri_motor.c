#include "dri_motor.h"
#include "motor.h"
#include "balance.h"
#include "key.h"
#include "safety.h"
#include "app_w25qxx.h"
#include "cmsis_os.h"

#if CTRL_DEBUG == 1
#include "elog.h"
#endif

/* ==================== 控制任务创建 ==================== */

void Control_Task_Create(void)
{
    xTaskCreate(Control_Task, "control", 512, NULL, tskIDLE_PRIORITY + 3, NULL);
}

/* ==================== 控制任务（10ms 周期） ==================== */

void Control_Task(void *pvParameters)
{
    (void)pvParameters;

    uint32_t last_save_tick = 0;
    #if CTRL_DEBUG == 1
    uint32_t last_status_tick = 0;
    #endif

    while (1)
    {
        /* 报警状态下不执行控制 */
        if (g_safety.alarm_state != ALARM_NONE) {
            osDelay(10);
            continue;
        }

        /* 急停（仅在运行时响应） */
        if (g_command.button_stop && g_command.direction != DIR_STOP) {
            Motor_Stop_All();
            g_command.direction = DIR_STOP;
            #if CTRL_DEBUG == 1
            elog_i("CTRL", "CMD_STOP");
            #endif
            osDelay(10);
            continue;
        }

        /* 上升（仅在停止态响应，与下降互斥） */
        if (g_command.button_up && !g_command.button_down
            && g_command.direction == DIR_STOP) {
            Motor_Start_All(DIR_UP);
            g_command.direction = DIR_UP;
            #if CTRL_DEBUG == 1
            elog_i("CTRL", "CMD_UP columns=%ld/%ld",
                   g_column[0].pulse_count, g_column[1].pulse_count);
            #endif
        }
        /* 下降（仅在停止态响应，与上升互斥） */
        else if (g_command.button_down && !g_command.button_up
                 && g_command.direction == DIR_STOP) {
            /* 二次下降保护 */
            if (g_safety.secondary_descent_triggered
                && !g_safety.secondary_descent_confirmed) {
                Buzzer_Beep(2000);
                osDelay(10);
                continue;
            }

            Motor_Start_All(DIR_DOWN);
            g_command.direction = DIR_DOWN;
            Buzzer_Beep(500);
            #if CTRL_DEBUG == 1
            elog_i("CTRL", "CMD_DOWN columns=%ld/%ld",
                   g_column[0].pulse_count, g_column[1].pulse_count);
            #endif
        }

        /* 运行时执行平衡 + 限位保护 */
        if (g_command.direction != DIR_STOP) {
            Balance_Run();
            Safety_Check_Upper_Limit();    /* 到顶自动停 */
            Safety_Check_Lower_Limit();
            Safety_Check_Secondary_Descent();
        }

        uint32_t now = HAL_GetTick();

        #if CTRL_DEBUG == 1
        /* 每 500ms 输出一次运行状态 */
        if (g_command.direction != DIR_STOP && now - last_status_tick > 500) {
            elog_i("CTRL", "RUN c0=%ld c1=%ld dir=%d stall",
                   g_column[0].pulse_count, g_column[1].pulse_count, g_command.direction);
            last_status_tick = now;
        }
        #endif

        /* 每 5 秒存一次高度 */
        if (now - last_save_tick > 5000) {
            App_W25Qxx_Height_Save();
            last_save_tick = now;
        }

        osDelay(10);
    }
}

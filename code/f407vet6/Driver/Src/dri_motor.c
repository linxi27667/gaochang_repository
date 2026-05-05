#include "dri_motor.h"
#include "dri_debug.h"
#include "motor.h"
#include "balance.h"
#include "key.h"
#include "safety.h"
#include "app_w25qxx.h"
#include "cmsis_os.h"
#include "main.h"

#if CTRL_DEBUG == 1
#include "elog.h"
#endif

#define HEIGHT_MM(p)  ((int32_t)(p) * (int32_t)g_config.screw_lead_mm)

void Control_Task_Create(void)
{
    xTaskCreate(Control_Task, "control", 512, NULL, tskIDLE_PRIORITY + 3, NULL);
}

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/* 告警处理：碰撞→下降键复位，其余→阻塞
 * 返回 1=已处理(continue), 0=告警已清除继续 */
static uint8_t Alarm_Handle(void)
{
    if (g_safety.alarm == ALARM_NONE)
        return 0;

    if (g_safety.alarm == ALARM_COLLISION
        && g_command.button_down && !g_command.button_up) {
        Safety_Alarm_Reset();
        g_safety.at_upper_limit = 0;
        return 0;   /* 清完告警，下周期正常走 */
    }

    return 1;   /* 阻塞 */
}


/* 松开检查：松手即停 */
static void Jog_Release_Check(uint8_t *limit_printed)
{
    if (!g_command.button_up && g_command.direction == DIR_UP) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        *limit_printed = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: STOP");
        #endif
    }
    if (!g_command.button_down && g_command.direction == DIR_DOWN) {
        Motor_Stop_All();
        g_command.direction = DIR_STOP;
        *limit_printed = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: STOP");
        #endif
    }
}

/* 启动检查：按住按键 + 限位守卫 */
static void Jog_Start_Check(uint8_t *limit_printed)
{
    /* ---- 上升 ---- */
    if (g_command.button_up && !g_command.button_down
        && g_command.direction == DIR_STOP) {

        if (g_safety.at_upper_limit) {
            #if CTRL_DEBUG == 1
            if (!*limit_printed) {
                elog_i("CTRL", "At top (%dmm)", HEIGHT_MM(g_config.max_pulses));
                *limit_printed = 1;
            }
            #endif
            return;
        }

        Motor_Start_All(DIR_UP);
        g_command.direction = DIR_UP;
        g_safety.at_lower_limit = 0;
        *limit_printed = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: UP");
        #endif
        return;
    }

    /* ---- 下降 ---- */
    if (g_command.button_down && !g_command.button_up
        && g_command.direction == DIR_STOP) {

        if (g_safety.at_lower_limit) {
            #if CTRL_DEBUG == 1
            if (!*limit_printed) {
                elog_i("CTRL", "At bottom");
                *limit_printed = 1;
            }
            #endif
            return;
        }

        if (g_safety.secondary_descent_triggered
            && !g_safety.secondary_descent_confirmed) {
            Buzzer_Beep(2000);
            return;
        }

        Motor_Start_All(DIR_DOWN);
        g_command.direction = DIR_DOWN;
        g_safety.at_upper_limit = 0;
        Buzzer_Beep(500);
        *limit_printed = 0;
        #if CTRL_DEBUG == 1
        elog_i("CTRL", "Status: DOWN");
        #endif
    }
}

/* 运转：编码器 → 高度 → 平衡 → 安全检查 */
static void Running_Update(void)
{
    if (g_command.direction == DIR_STOP) return;

    Sim_Encoder_Run();

    #if CTRL_DEBUG == 1
    elog_i("CTRL", "Height: %ldmm/%ldmm",
           HEIGHT_MM(g_column[0].pulse_count),
           HEIGHT_MM(g_column[1].pulse_count));
    #endif

    Balance_Run();
    Sim_Collision_Check();
    Safety_Check_Upper_Limit();
    Safety_Check_Lower_Limit();
    Safety_Check_Secondary_Descent();
}

/* Flash 存储：高度变化 + 间隔>5s */
static void Flash_Save_If_Needed(uint32_t now,
    uint32_t *last_save_tick,
    int32_t last_saved[2])
{
    if (g_command.direction == DIR_STOP) return;

    if (g_column[0].pulse_count != last_saved[0]
        || g_column[1].pulse_count != last_saved[1]) {
        if (now - *last_save_tick > 5000) {
            App_W25Qxx_Height_Save();
            last_saved[0] = g_column[0].pulse_count;
            last_saved[1] = g_column[1].pulse_count;
            *last_save_tick = now;
        }
    }
}

/* ================================================================
 *  Control_Task — 10ms
 * ================================================================ */

void Control_Task(void *pvParameters)
{
    (void)pvParameters;

    uint32_t last_save_tick = 0;
    int32_t  last_saved[2]  = {0, 0};
    uint8_t  limit_printed  = 0;

    while (1)
    {
        /* 1. 告警 */
        if (Alarm_Handle()) { osDelay(10); continue; }

        /* 2. 双键冲突 → 停机 */
        if (g_command.button_up && g_command.button_down
            && g_command.direction != DIR_STOP) {
            Motor_Stop_All();
            g_command.direction = DIR_STOP;
            /* 松任一键可立即重启 */
            limit_printed = 0;
            #if CTRL_DEBUG == 1
            elog_i("CTRL", "Status: STOP (conflict)");
            #endif
        }

        /* 4. 点动：松停 + 按启 */
        Jog_Release_Check(&limit_printed);
        Jog_Start_Check(&limit_printed);

        /* 5. 运转 */
        Running_Update();

        /* 6. 存储 */
        Flash_Save_If_Needed(HAL_GetTick(), &last_save_tick, last_saved);

        osDelay(10);
    }
}

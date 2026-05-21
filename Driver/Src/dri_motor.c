#include "dri_motor.h"
#include "key.h"
#include "safety.h"
#include "motor.h"
#include "app_w25qxx.h"
#include "cmsis_os.h"

#if CTRL_DEBUG == 1
#include "elog.h"
#endif

void Control_Task_Create(void)
{
    xTaskCreate(Control_Task, "control", 512, NULL, tskIDLE_PRIORITY + 3, NULL);
}

static const char *ctrl_dir_name(direction_t direction)
{
    if (direction == DIR_UP) return "UP";
    if (direction == DIR_DOWN) return "DOWN";
    return "STOP";
}

void Control_Task(void *pvParameters)
{
    (void)pvParameters;
    static uint32_t led_cnt = 0;
    static direction_t last_dir = DIR_STOP;
    static int32_t last_left_mm = 0;
    static int32_t last_right_mm = 0;
    static uint32_t last_motion_log_tick = 0;

    while (1)
    {
        if (Safety_Alarm_Handle()) {
            osDelay(10);
            continue;
        }

        Safety_Check_Collision();
        Key_Jog_Conflict_Check();
        Key_Jog_Release_Check();
        Key_Jog_Start_Check();

        Safety_Running_Update();
        App_W25Qxx_Height_Save_If_Needed();

        int32_t left_mm = HEIGHT_MM(g_column[0].pulse_count);
        int32_t right_mm = HEIGHT_MM(g_column[1].pulse_count);

        #if CTRL_DEBUG == 1
        if (last_dir != g_command.direction) {
            elog_i("CTRL", "state %s left=%ldmm right=%ldmm diff=%ld",
                   ctrl_dir_name(g_command.direction), left_mm, right_mm, left_mm - right_mm);
            last_dir = g_command.direction;
            last_left_mm = left_mm;
            last_right_mm = right_mm;
            last_motion_log_tick = HAL_GetTick();
        } else if (g_command.direction != DIR_STOP &&
                   (HAL_GetTick() - last_motion_log_tick >= 1000) &&
                   (left_mm != last_left_mm || right_mm != last_right_mm)) {
            elog_i("HEIGHT", "dir=%s left=%ldmm right=%ldmm pulse=%ld/%ld diff=%ld",
                   ctrl_dir_name(g_command.direction), left_mm, right_mm,
                   g_column[0].pulse_count, g_column[1].pulse_count, left_mm - right_mm);
            last_left_mm = left_mm;
            last_right_mm = right_mm;
            last_motion_log_tick = HAL_GetTick();
        }
        #endif

        led_cnt++;
        if (led_cnt >= 50) {
            led_cnt = 0;
            LED_RUN_TOGGLE();
        }

        osDelay(10);
    }
}

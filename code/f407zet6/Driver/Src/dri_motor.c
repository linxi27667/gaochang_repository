#include "dri_motor.h"
#include "key.h"
#include "safety.h"
#include "motor.h"
#include "cmsis_os.h"

#if CTRL_DEBUG == 1
#include "elog.h"
#endif

void Control_Task_Create(void)
{
    xTaskCreate(Control_Task, "control", 512, NULL, tskIDLE_PRIORITY + 3, NULL);
}

void Control_Task(void *pvParameters)
{
    (void)pvParameters;
    static uint32_t hb_cnt = 0;
    static uint32_t led_cnt = 0;

    while (1)
    {
        if (Safety_Alarm_Handle()) { osDelay(10); continue; }

        Key_Jog_Conflict_Check();
        Key_Jog_Release_Check();
        Key_Jog_Start_Check();

        Safety_Running_Update();

        /* App_W25Qxx_Height_Save_If_Needed(); -- 屏蔽 */

        #if CTRL_DEBUG == 1
        if (g_command.direction != DIR_STOP) {
            elog_i("HEIGHT", "L=%ldmm R=%ldmm",
                   g_column[0].pulse_count * SCREW_LEAD_MM,
                   g_column[1].pulse_count * SCREW_LEAD_MM);
        }
        #endif

        hb_cnt++;
        led_cnt++;
        if (led_cnt >= 50) {							
            led_cnt = 0;
            LED_RUN_TOGGLE();
        }
        if (hb_cnt >= 100) {
            hb_cnt = 0;
            elog_i("CTRL", "HB dir=%d L=%ld R=%ld",
                   g_command.direction,
                   g_column[0].pulse_count * SCREW_LEAD_MM,
                   g_column[1].pulse_count * SCREW_LEAD_MM);
        }

        osDelay(10);
    }
}

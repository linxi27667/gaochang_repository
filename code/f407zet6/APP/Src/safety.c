#include "safety.h"
#include "motor.h"
#include "encoder.h"
#include "key.h"
#include "app_w25qxx.h"

#if SAFETY_DEBUG == 1 || CTRL_DEBUG == 1
#include "elog.h"
#endif
#include "cmsis_os.h"
#include "balance.h"

safety_state_t g_safety = { .alarm = ALARM_NONE };

void Safety_Init(void)
{
    g_safety.secondary_descent_triggered   = 0;
    g_safety.secondary_descent_confirmed   = 0;
    g_safety.at_lower_limit                = 0;
    g_safety.at_upper_limit                = 0;
    g_safety.stall_suspected               = 0;
    g_safety.alarm                         = ALARM_NONE;
}

void Safety_EXTI_Handler(uint16_t gpio_pin)
{
    (void)gpio_pin;
    #if COLLISION_ENABLE == 1
    /* 防碰杆EXTI处理 - 真机启用时实现 */
    #endif
}

void Safety_Check_Stall(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;

        if (now - g_safety.last_pulse_tick[i] > g_config.stall_timeout_ms) {
            Motor_Stop_All();
            g_safety.alarm           = ALARM_STALL;
            g_safety.stall_suspected = 1;
            #if SAFETY_DEBUG == 1
            elog_e("SAFETY", "Stall! col=%d", i);
            #endif
        }
    }
}

#if SECONDARY_DESCENT_ENABLE == 1
void Safety_Check_Secondary_Descent(void)
{
    if (g_command.direction != DIR_DOWN) return;
    if (g_safety.secondary_descent_triggered) return;

    int32_t height_0 = Encoder_Get_Count(0);
    int32_t height_1 = Encoder_Get_Count(1);
    uint16_t pulses = g_config.secondary_descent_pulses;

    if (height_0 <= (int32_t)pulses || height_1 <= (int32_t)pulses) {
        Motor_Stop_All();
        g_safety.secondary_descent_triggered = 1;
        #if SAFETY_DEBUG == 1
        elog_w("SAFETY", "Near bottom, confirm to continue");
        #endif
    }
}
#else
void Safety_Check_Secondary_Descent(void) {}
#endif

void Safety_Check_Upper_Limit(void)
{
    if (g_command.direction != DIR_UP) return;

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;
        if (g_column[i].pulse_count >= (int32_t)g_config.max_pulses) {
            Motor_Stop_All();
            g_command.direction = DIR_STOP;
            g_safety.at_upper_limit = 1;
            #if SAFETY_DEBUG == 1
            elog_w("SAFETY", "Top reached (%ldmm)", (int32_t)g_config.max_pulses * SCREW_LEAD_MM);
            #endif
            return;
        }
    }
}

void Safety_Check_Lower_Limit(void)
{
    if (g_command.direction != DIR_DOWN) return;

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;
        if (g_column[i].pulse_count <= 0) {
            Motor_Stop_All();
            g_command.direction = DIR_STOP;
            Encoder_Reset_Count(0);
            Encoder_Reset_Count(1);
            g_safety.at_lower_limit = 1;
            #if SAFETY_DEBUG == 1
            elog_w("SAFETY", "Bottom reached");
            #endif
            return;
        }
    }
}

void Safety_Alarm_Reset(void)
{
    if (g_safety.alarm == ALARM_NONE) return;

    g_safety.alarm                       = ALARM_NONE;
    g_safety.stall_suspected             = 0;
    g_safety.secondary_descent_triggered = 0;
    g_safety.secondary_descent_confirmed = 0;
    g_command.direction                  = DIR_STOP;
    #if SAFETY_DEBUG == 1
    elog_i("SAFETY", "Alarm cleared");
    #endif
}

uint8_t Safety_Alarm_Handle(void)
{
    if (g_safety.alarm == ALARM_NONE)
        return 0;

    if (g_safety.alarm == ALARM_COLLISION
        && g_command.button_down && !g_command.button_up) {
        Safety_Alarm_Reset();
        g_safety.at_upper_limit = 0;
        return 0;
    }

    return 1;
}

void Safety_Running_Update(void)
{
    if (g_command.direction == DIR_STOP) return;

    Balance_Run();
    Safety_Check_Upper_Limit();
    Safety_Check_Lower_Limit();
    Safety_Check_Secondary_Descent();
}

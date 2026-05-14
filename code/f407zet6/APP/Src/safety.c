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
#include "dri_debug.h"

/* ==================== 全局变量 ==================== */

safety_state_t g_safety = { .alarm = ALARM_NONE };

/* ==================== 内部变量 ==================== */

#if COLLISION_ENABLE == 1
static uint32_t collision_debounce_tick = 0;
#endif

/* ==================== 初始化 ==================== */

void Safety_Init(void)
{
    #if COLLISION_ENABLE == 1
    collision_debounce_tick = HAL_GetTick();
    #endif
    g_safety.secondary_descent_triggered   = 0;
    g_safety.secondary_descent_confirmed   = 0;
    g_safety.at_lower_limit                = 0;
    g_safety.at_upper_limit                = 0;
    g_safety.stall_suspected               = 0;
    g_safety.alarm                         = ALARM_NONE;
}

/* ==================== EXTI 中断回调 ==================== */

void Safety_EXTI_Handler(uint16_t gpio_pin)
{
    if (gpio_pin == LOWER_LIMIT_PIN) {
        Encoder_Reset_Count(0);
        Encoder_Reset_Count(1);
        g_safety.at_lower_limit = 1;
        #if SAFETY_DEBUG == 1
        elog_i("SAFETY", "Bottom zero");
        #endif
    }
    #if COLLISION_ENABLE == 1
    else if (gpio_pin == COLLISION_1_PIN || gpio_pin == COLLISION_2_PIN) {
        uint32_t now = HAL_GetTick();
        if (now - collision_debounce_tick < g_config.collision_debounce_ms)
            return;
        collision_debounce_tick = now;

        Motor_Stop_All_Immediate();
        g_safety.alarm = ALARM_COLLISION;
        Buzzer_On();
        #if SAFETY_DEBUG == 1
        elog_e("SAFETY", "Collision! pin=0x%04X", gpio_pin);
        #endif
    }
    #endif
}

/* ==================== 堵转检测 ==================== */

void Safety_Check_Stall(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;

        if (now - g_safety.last_pulse_tick[i] > g_config.stall_timeout_ms) {
            Motor_Stop_All();
            g_safety.alarm           = ALARM_STALL;
            g_safety.stall_suspected = 1;
            Buzzer_On();
            #if SAFETY_DEBUG == 1
            elog_e("SAFETY", "Stall! col=%d", i);
            #endif
        }
    }
}

/* ==================== 二次下降保护 ==================== */

void Safety_Check_Secondary_Descent(void)
{
    #if SECONDARY_DESCENT_ENABLE == 0
    return;   /* 调试阶段关闭，真机改 1 启用 */
    #else
    if (g_command.direction != DIR_DOWN) return;
    if (g_safety.secondary_descent_triggered) return;

    int32_t height_0 = Encoder_Get_Count(0);
    int32_t height_1 = Encoder_Get_Count(1);
    uint16_t pulses = g_config.secondary_descent_pulses;

    if (height_0 <= (int32_t)pulses || height_1 <= (int32_t)pulses) {
        Motor_Stop_All();
        g_safety.secondary_descent_triggered = 1;
        Buzzer_Beep(2000);
        #if SAFETY_DEBUG == 1
        elog_w("SAFETY", "Near bottom, confirm to continue");
        #endif
    }
    #endif
}

/* ==================== 上限位保护（不设告警，只停上升） ==================== */

void Safety_Check_Upper_Limit(void)
{
    if (g_command.direction != DIR_UP) return;
    if (!g_safety.at_upper_limit) g_safety.at_upper_limit = 0;

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;
        if (g_column[i].pulse_count >= (int32_t)g_config.max_pulses) {
            Motor_Stop_All();
            g_command.direction = DIR_STOP;
            g_safety.at_upper_limit = 1;
            #if SAFETY_DEBUG == 1
            elog_w("SAFETY", "Top reached (%dmm)", g_config.max_pulses * g_config.screw_lead_mm);
            #endif
            return;
        }
    }
}

/* ==================== 下限位保护（不设告警，只停下降） ==================== */

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

/* ==================== 报警复位 ==================== */

void Safety_Alarm_Reset(void)
{
    if (g_safety.alarm == ALARM_NONE) return;

    g_safety.alarm                       = ALARM_NONE;
    g_safety.stall_suspected             = 0;
    g_safety.secondary_descent_triggered = 0;
    g_safety.secondary_descent_confirmed = 0;
    g_command.direction                  = DIR_STOP;
    Buzzer_Off();
    #if SAFETY_DEBUG == 1
    elog_i("SAFETY", "Alarm cleared");
    #endif
}

/* ==================== 告警处理 ==================== */
/* 返回 1=已处理(阻塞), 0=告警已清除继续 */

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

/* ==================== 运转更新 ==================== */

void Safety_Running_Update(void)
{
    if (g_command.direction == DIR_STOP) return;

    Sim_Encoder_Run();

    #if SAFETY_DEBUG == 1
    elog_i("SAFETY", "Height: %ldmm/%ldmm",
           HEIGHT_MM(g_column[0].pulse_count),
           HEIGHT_MM(g_column[1].pulse_count));
    #endif

    Balance_Run();
    Sim_Collision_Check();
    Safety_Check_Upper_Limit();
    Safety_Check_Lower_Limit();
    Safety_Check_Secondary_Descent();
}

#include "safety.h"
#include "motor.h"
#include "encoder.h"
#include "key.h"
#include "app_w25qxx.h"

#if SAFETY_DEBUG == 1
#include "elog.h"
#endif
#include "cmsis_os.h"

/* ==================== 全局变量 ==================== */

safety_state_t g_safety = {0};

/* ==================== 内部变量 ==================== */

static uint32_t collision_debounce_tick = 0;

/* ==================== 初始化 ==================== */

void Safety_Init(void)
{
    g_safety.collision_triggered           = 0;
    g_safety.stall_detected                = 0;
    g_safety.balance_timeout               = 0;
    g_safety.secondary_descent_triggered   = 0;
    g_safety.secondary_descent_confirmed   = 0;
    g_safety.at_lower_limit                = 0;
    g_safety.alarm_state                   = ALARM_NONE;
}

/* ==================== EXTI 中断回调 ==================== */

void Safety_EXTI_Handler(uint16_t gpio_pin)
{
    if (gpio_pin == LOWER_LIMIT_PIN) {
        /* 下限位触发：清零两个编码器 */
        Encoder_Reset_Count(0);
        Encoder_Reset_Count(1);
        g_safety.at_lower_limit = 1;
        #if SAFETY_DEBUG == 1
        elog_i("SAFETY", "LOWER_LIMIT triggered, counters reset");
        #endif
    }
    else if (gpio_pin == COLLISION_1_PIN || gpio_pin == COLLISION_2_PIN) {
        /* 防碰杆触发：去抖 */
        uint32_t now = HAL_GetTick();
        if (now - collision_debounce_tick < g_config.collision_debounce_ms)
            return;
        collision_debounce_tick = now;

        Motor_Stop_All_Immediate();
        g_safety.collision_triggered = 1;
        g_safety.alarm_state = ALARM_COLLISION;
        Buzzer_On();
        #if SAFETY_DEBUG == 1
        elog_e("SAFETY", "COLLISION pin=0x%04X", gpio_pin);
        #endif
    }
}

/* ==================== 堵转检测 ==================== */

void Safety_Check_Stall(void)
{
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;

        if (now - g_safety.last_pulse_tick[i] > g_config.stall_timeout_ms) {
            Motor_Stop_All();
            g_safety.stall_detected = 1;
            g_safety.alarm_state = ALARM_STALL;
            Buzzer_On();
            #if SAFETY_DEBUG == 1
            elog_e("SAFETY", "STALL col=%d last=%lu now=%lu", i, g_safety.last_pulse_tick[i], now);
            #endif
        }
    }
}

/* ==================== 二次下降保护 ==================== */

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
        Buzzer_Beep(2000);
        #if SAFETY_DEBUG == 1
        elog_w("SAFETY", "2ND_DESCENT triggered c0=%d c1=%d threshold=%d", height_0, height_1, pulses);
        #endif
    }
}

/* ==================== 上限位保护 ==================== */

void Safety_Check_Upper_Limit(void)
{
    if (g_command.direction != DIR_UP) return;

    if (g_column[0].motor_state != MOTOR_RUNNING
        && g_column[1].motor_state != MOTOR_RUNNING) return;

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;

        if (g_column[i].pulse_count >= (int32_t)g_config.max_pulses) {
            Motor_Stop_All();
            g_safety.alarm_state = ALARM_UPPER_LIMIT;
            Buzzer_On();
            #if SAFETY_DEBUG == 1
            elog_e("SAFETY", "UPPER_LIMIT col=%d count=%ld max=%d", i, g_column[i].pulse_count, g_config.max_pulses);
            #endif
            return;
        }
    }
}

/* ==================== 下限位保护 ==================== */

void Safety_Check_Lower_Limit(void)
{
    if (g_command.direction != DIR_DOWN) return;

    for (int i = 0; i < 2; i++) {
        if (g_column[i].motor_state != MOTOR_RUNNING) continue;

        if (g_column[i].pulse_count <= 0) {
            Motor_Stop_All();
            g_safety.at_lower_limit = 1;
            Buzzer_Beep(500);
            #if SAFETY_DEBUG == 1
            elog_w("SAFETY", "LOWER_LIMIT col=%d count=%ld", i, g_column[i].pulse_count);
            #endif
            return;
        }
    }
}

/* ==================== 报警复位 ==================== */

void Safety_Alarm_Reset(void)
{
    if (g_safety.alarm_state == ALARM_NONE) return;

    g_safety.alarm_state                 = ALARM_NONE;
    g_safety.collision_triggered         = 0;
    g_safety.stall_detected              = 0;
    g_safety.balance_timeout             = 0;
    g_safety.secondary_descent_triggered = 0;
    g_safety.secondary_descent_confirmed = 0;
    g_command.direction                  = DIR_STOP;
    Buzzer_Off();
    #if SAFETY_DEBUG == 1
    elog_i("SAFETY", "ALARM_RESET state=%d", g_safety.alarm_state);
    #endif
}

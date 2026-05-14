#include "dri_debug.h"
#include "motor.h"
#include "key.h"
#include "safety.h"
#include "app_w25qxx.h"

static uint32_t g_last_pulse_tick = 0;

void Sim_Encoder_Init(void)
{
    g_last_pulse_tick = 0;
}

/* ==================== 模拟编码器 ==================== */
/* 只在 g_command.direction != STOP 时由 Control_Task 每 10ms 调用 */
/* 每 50ms 为正在运行的柱子 +1/-1 脉冲 */
/* 模拟 0#柱比 1#柱快 5:4，用于测试平衡算法 */

void Sim_Encoder_Run(void)
{
    uint32_t now = HAL_GetTick();

    if (g_command.direction == DIR_STOP) {
        g_last_pulse_tick = now;
        return;
    }

    if (now - g_last_pulse_tick < SIM_PULSE_INTERVAL_MS)
        return;

    g_last_pulse_tick = now;
    static uint8_t skip_counter = 0;
    skip_counter++;

    if (g_command.direction == DIR_UP) {
        /* 0#柱：只要在跑就发脉冲 */
        if (g_column[0].motor_state == MOTOR_RUNNING) {
            g_column[0].pulse_count++;
            g_column[0].last_pulse_tick = now;
            g_safety.last_pulse_tick[0] = now;
        }

        /* 1#柱：每 5 个脉冲漏 1 个，模拟硬件差异 */
        if (g_column[1].motor_state == MOTOR_RUNNING
            && skip_counter % 5 != 0) {
            g_column[1].pulse_count++;
            g_column[1].last_pulse_tick = now;
            g_safety.last_pulse_tick[1] = now;
        }

        if (g_safety.at_lower_limit) g_safety.at_lower_limit = 0;
    }
    else if (g_command.direction == DIR_DOWN) {
        /* 模拟0#柱比1#柱降得慢（与上升对称）：每5个脉冲漏0#1个 */
        if (g_column[0].motor_state == MOTOR_RUNNING
            && g_column[0].pulse_count > 0
            && skip_counter % 5 != 0) {
            g_column[0].pulse_count--;
            g_column[0].last_pulse_tick = now;
            g_safety.last_pulse_tick[0] = now;
        }
        if (g_column[1].motor_state == MOTOR_RUNNING
            && g_column[1].pulse_count > 0) {
            g_column[1].pulse_count--;
            g_column[1].last_pulse_tick = now;
            g_safety.last_pulse_tick[1] = now;
        }

        if (g_safety.at_upper_limit) g_safety.at_upper_limit = 0;
    }
}

/* ==================== 模拟防碰杆 ==================== */

void Sim_Collision_Check(void)
{
    #if COLLISION_ENABLE == 1
    return;   /* 真机：实物 EXTI 接管 */
    #endif

    /* 只有上升时检查（下降不会碰防碰杆） */
    if (g_command.direction != DIR_UP) return;

    int32_t h0_mm = g_column[0].pulse_count * g_config.screw_lead_mm;
    int32_t h1_mm = g_column[1].pulse_count * g_config.screw_lead_mm;

    if (h0_mm >= SIM_COLLISION_HEIGHT_MM || h1_mm >= SIM_COLLISION_HEIGHT_MM) {
        Motor_Stop_All_Immediate();
        g_safety.alarm = ALARM_COLLISION;
        Buzzer_On();
        #if SAFETY_DEBUG == 1
        elog_e("SAFETY", "Collision! sim at %dmm", SIM_COLLISION_HEIGHT_MM);
        #endif
    }
}

#include "encoder.h"
#include "motor.h"
#include "safety.h"
#include "tim.h"

/* ==================== 初始化 ==================== */

void Encoder_Init(void)
{
    HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);
    HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_2);
}

/* ==================== 捕获中断回调 ==================== */

void Encoder_Capture_ISR(uint8_t channel)
{
    uint32_t tick = HAL_GetTick();

    if (channel == 1) {
        if (g_column[0].counting_enable) {
            g_column[0].pulse_count++;
            g_column[0].last_pulse_tick = tick;
            g_safety.last_pulse_tick[0] = tick;
        }
    } else if (channel == 2) {
        if (g_column[1].counting_enable) {
            g_column[1].pulse_count++;
            g_column[1].last_pulse_tick = tick;
            g_safety.last_pulse_tick[1] = tick;
        }
    }
}

/* ==================== 读脉冲数（原子操作） ==================== */

int32_t Encoder_Get_Count(uint8_t column_index)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int32_t count = g_column[column_index].pulse_count;
    __set_PRIMASK(primask);
    return count;
}

/* ==================== 清零脉冲数 ==================== */

void Encoder_Reset_Count(uint8_t column_index)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_column[column_index].pulse_count = 0;
    __set_PRIMASK(primask);
}

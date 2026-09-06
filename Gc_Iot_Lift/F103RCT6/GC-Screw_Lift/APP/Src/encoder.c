#include "encoder.h"
#include "motor.h"
#include "safety.h"
#include "key.h"
#include "tim.h"
#include "main.h"

void Encoder_Init(void)
{
    HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_3);
    HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_4);
}

void Encoder_Capture_ISR(uint8_t channel)
{
    uint32_t tick = HAL_GetTick();

    int8_t delta = (g_command.direction == DIR_DOWN) ? -1 : 1;

    if (channel == 3) {
        if (g_column[0].counting_enable) {
            g_column[0].pulse_count += delta;
            g_column[0].last_pulse_tick = tick;
            g_safety.last_pulse_tick[0] = tick;
        }
    } else if (channel == 4) {
        if (g_column[1].counting_enable) {
            g_column[1].pulse_count += delta;
            g_column[1].last_pulse_tick = tick;
            g_safety.last_pulse_tick[1] = tick;
        }
    }
}

int32_t Encoder_Get_Count(uint8_t column_index)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int32_t count = g_column[column_index].pulse_count;
    __set_PRIMASK(primask);
    return count;
}

void Encoder_Reset_Count(uint8_t column_index)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_column[column_index].pulse_count = 0;
    __set_PRIMASK(primask);
}

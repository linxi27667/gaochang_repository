#include "fake_hal.h"
#include "stm32f4xx_hal.h"

static uint32_t s_fake_tick;

uint32_t HAL_GetTick(void)
{
    return s_fake_tick;
}

void fake_hal_reset(void)
{
    s_fake_tick = 0U;
}

void fake_hal_advance_ms(uint32_t ms)
{
    s_fake_tick += ms;
}

void fake_hal_set_tick(uint32_t tick)
{
    s_fake_tick = tick;
}

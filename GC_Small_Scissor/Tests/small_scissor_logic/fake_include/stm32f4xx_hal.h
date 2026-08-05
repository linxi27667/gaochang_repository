#ifndef TEST_STM32F4XX_HAL_H
#define TEST_STM32F4XX_HAL_H

#include <stdint.h>

uint32_t HAL_GetTick(void);

static inline uint32_t __get_PRIMASK(void)
{
    return 0U;
}

static inline void __disable_irq(void)
{
}

static inline void __set_PRIMASK(uint32_t primask)
{
    (void)primask;
}

#endif

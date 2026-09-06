#ifndef TEST_FAKE_HAL_H
#define TEST_FAKE_HAL_H

#include <stdint.h>

void fake_hal_reset(void);
void fake_hal_advance_ms(uint32_t ms);
void fake_hal_set_tick(uint32_t tick);

#endif

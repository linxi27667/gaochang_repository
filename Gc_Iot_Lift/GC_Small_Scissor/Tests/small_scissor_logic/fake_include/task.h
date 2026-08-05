#ifndef TEST_FREERTOS_TASK_H
#define TEST_FREERTOS_TASK_H

#include "FreeRTOS.h"
#include "fake_hal.h"

static inline void vTaskDelay(TickType_t ticks)
{
    fake_hal_advance_ms(ticks);
}

#endif

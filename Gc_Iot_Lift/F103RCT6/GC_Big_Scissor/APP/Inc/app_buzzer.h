#ifndef APP_BUZZER_H
#define APP_BUZZER_H

#include <stdint.h>

extern volatile uint8_t g_app_buzzer_on;

static inline void App_Buzzer_Alarm(uint16_t duration_ms)
{
    (void)duration_ms;
    g_app_buzzer_on = 1U;
}

static inline void App_Buzzer_Off(void)
{
    g_app_buzzer_on = 0U;
}

static inline uint8_t App_Buzzer_IsOn(void)
{
    return g_app_buzzer_on ? 1U : 0U;
}

#endif

#ifndef APP_BUZZER_H
#define APP_BUZZER_H

#include <stdint.h>

/* F103 无独立蜂鸣器硬件：保留接口避免链接/编译失败 */
static uint8_t s_app_buzzer_on;

static inline void App_Buzzer_Alarm(uint16_t duration_ms)
{
    (void)duration_ms;
    s_app_buzzer_on = 1U;
}

static inline void App_Buzzer_Off(void)
{
    s_app_buzzer_on = 0U;
}

static inline uint8_t App_Buzzer_IsOn(void)
{
    return s_app_buzzer_on ? 1U : 0U;
}

#endif

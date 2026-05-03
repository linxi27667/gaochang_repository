#include "key.h"
#include "motor.h"

#if KEY_DEBUG == 1
#include "elog.h"
#endif

/* ==================== 全局变量 ==================== */

key_command_t g_command = {0};

/* ==================== 按键去抖 ==================== */

#define KEY_DEBOUNCE_COUNT  3

static uint8_t Key_Read_Pin(GPIO_TypeDef *port, uint16_t pin)
{
    /* 按下为低电平（外部上拉），返回 1=按下 0=释放 */
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1 : 0;
}

static uint8_t Key_Debounce(GPIO_TypeDef *port, uint16_t pin)
{
    uint8_t count = 0;
    for (int i = 0; i < KEY_DEBOUNCE_COUNT; i++) {
        if (Key_Read_Pin(port, pin))
            count++;
        HAL_Delay(1);
    }
    return (count >= KEY_DEBOUNCE_COUNT) ? 1 : 0;
}

/* ==================== 初始化 ==================== */

void Key_Init(void)
{
    Buzzer_Off();
}

/* ==================== 按键扫描 ==================== */

void Key_Scan(void)
{
    uint8_t old_up      = g_command.button_up;
    uint8_t old_down    = g_command.button_down;
    uint8_t old_stop    = g_command.button_stop;
    uint8_t old_confirm = g_command.button_confirm;

    g_command.button_up      = Key_Debounce(BUTTON_UP_PORT,      BUTTON_UP_PIN);
    g_command.button_down    = Key_Debounce(BUTTON_DOWN_PORT,    BUTTON_DOWN_PIN);
    g_command.button_stop    = Key_Debounce(BUTTON_STOP_PORT,    BUTTON_STOP_PIN);
    g_command.button_confirm = Key_Debounce(BUTTON_CONFIRM_PORT, BUTTON_CONFIRM_PIN);

    #if KEY_DEBUG == 1
    if (g_command.button_up      != old_up)      elog_i("KEY", "UP=%d",      g_command.button_up);
    if (g_command.button_down    != old_down)    elog_i("KEY", "DOWN=%d",    g_command.button_down);
    if (g_command.button_stop    != old_stop)    elog_i("KEY", "STOP=%d",    g_command.button_stop);
    if (g_command.button_confirm != old_confirm) elog_i("KEY", "CONFIRM=%d", g_command.button_confirm);
    #endif
}

/* ==================== 蜂鸣器 ==================== */

static uint32_t buzzer_off_tick = 0;

void Buzzer_On(void)
{
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);
    #if KEY_DEBUG == 1
    elog_i("KEY", "BUZZER ON");
    #endif
}

void Buzzer_Off(void)
{
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
    #if KEY_DEBUG == 1
    elog_i("KEY", "BUZZER OFF");
    #endif
}

void Buzzer_Beep(uint32_t duration_ms)
{
    Buzzer_On();
    buzzer_off_tick = HAL_GetTick() + duration_ms;
}

void Buzzer_Poll(void)
{
    if (buzzer_off_tick && HAL_GetTick() >= buzzer_off_tick) {
        Buzzer_Off();
        buzzer_off_tick = 0;
    }
}

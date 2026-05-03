#include "stm32f4xx_hal.h"
#include <assert.h>

/* ==================== 模拟引脚状态存储 ==================== */

/* 初始化为高电平模拟上拉（未按下），模式为输入 */
static uint8_t pin_states[0x400] = {[0 ... 0x3FF] = 1};
static uint8_t pin_modes[0x400]  = {0};
static uint32_t sim_tick = 0;                  /* 模拟 systick */
static uint32_t sim_primask = 0;               /* 模拟 PRIMASK */

/* ==================== GPIO ==================== */

static inline int pin_index(GPIO_TypeDef *port, uint16_t pin)
{
    int base = ((uintptr_t)port >> 4) & 0x3F;
    int idx = 0;
    while (pin) { pin >>= 1; idx++; }
    return base * 16 + idx - 1;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, uint8_t state)
{
    int i = pin_index(port, pin);
    if (i >= 0 && i < (int)sizeof(pin_states)) {
        pin_states[i] = state;
        pin_modes[i] = 1;
    }
}

uint8_t HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    int i = pin_index(port, pin);
    if (i >= 0 && i < (int)sizeof(pin_states)) {
        return pin_states[i];
    }
    return 0;
}

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    int base = ((uintptr_t)port >> 4) & 0x3F;
    uint16_t p = init->Pin;
    for (int idx = 0; p; p >>= 1, idx++) {
        if (p & 1) {
            int i = base * 16 + idx;
            pin_modes[i] = (init->Mode == GPIO_MODE_INPUT) ? 0 : 1;
            if (init->Pull == GPIO_PULLUP)
                pin_states[i] = 1;  /* 上拉默认高 */
            else
                pin_states[i] = 0;
        }
    }
}

/* ==================== RCC ==================== */

void __HAL_RCC_GPIOA_CLK_ENABLE(void) {}
void __HAL_RCC_GPIOB_CLK_ENABLE(void) {}
void __HAL_RCC_GPIOC_CLK_ENABLE(void) {}
void __HAL_RCC_GPIOH_CLK_ENABLE(void) {}
void __HAL_RCC_TIM2_CLK_ENABLE(void) {}
void __HAL_RCC_SPI1_CLK_ENABLE(void) {}
void __HAL_RCC_USART1_CLK_ENABLE(void) {}

/* ==================== SysTick ==================== */

uint32_t HAL_GetTick(void)
{
    return sim_tick;
}

void HAL_Delay(uint32_t ms)
{
    sim_tick += ms;
}

void sim_tick_advance(uint32_t ms)
{
    sim_tick += ms;
}

/* ==================== TIM ==================== */

TIM_HandleTypeDef htim2 = { .Instance = TIM2, .Channel = 0 };

HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *htim, uint32_t channel)
{
    (void)htim; (void)channel;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *htim)   { (void)htim; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_IC_Init(TIM_HandleTypeDef *htim)    { (void)htim; return HAL_OK; }
HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *htim, void *c) { (void)htim; (void)c; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *htim, void *c, uint32_t ch) { (void)htim; (void)c; (void)ch; return HAL_OK; }
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim) { (void)htim; }

/* ==================== 中断模拟 ==================== */

uint32_t __get_PRIMASK(void)     { return sim_primask; }
void __disable_irq(void)         { sim_primask = 1; }
void __set_PRIMASK(uint32_t m)   { sim_primask = m; }

/* ==================== 编码器脉冲注入 ==================== */

extern void Encoder_Capture_ISR(uint8_t channel);

void sim_encoder_pulse(uint8_t channel)
{
    Encoder_Capture_ISR(channel);
}

/* ==================== 按键注入 ==================== */

/* 按键都在 GPIOB 上 */
void sim_button_press(uint16_t pin)
{
    int i = pin_index(GPIOB, pin);
    if (i >= 0 && i < 0x400) {
        pin_states[i] = 0;  /* 按下 = 低电平 */
        pin_modes[i] = 0;   /* 输入模式 */
    }
}

void sim_button_release(uint16_t pin)
{
    int i = pin_index(GPIOB, pin);
    if (i >= 0 && i < 0x400) {
        pin_states[i] = 1;  /* 释放 = 高电平（上拉） */
        pin_modes[i] = 0;
    }
}

/* ==================== 调试工具 ==================== */

uint8_t sim_pin_read(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin);
}

static const char *pin_name(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == GPIOC && pin == GPIO_PIN_0) return "MOTOR1_UP";
    if (port == GPIOC && pin == GPIO_PIN_1) return "MOTOR2_UP";
    if (port == GPIOC && pin == GPIO_PIN_2) return "MOTOR1_MAIN";
    if (port == GPIOC && pin == GPIO_PIN_3) return "MOTOR2_MAIN";
    if (port == GPIOC && pin == GPIO_PIN_4) return "BRAKE";
    if (port == GPIOC && pin == GPIO_PIN_5) return "REVERSE";
    if (port == GPIOB && pin == GPIO_PIN_8)  return "BUZZER";
    return "?";
}

void sim_pin_print_all(void)
{
    const struct { GPIO_TypeDef *port; uint16_t pin; } pins[] = {
        {GPIOC, GPIO_PIN_0}, {GPIOC, GPIO_PIN_1}, {GPIOC, GPIO_PIN_2},
        {GPIOC, GPIO_PIN_3}, {GPIOC, GPIO_PIN_4}, {GPIOC, GPIO_PIN_5},
        {GPIOB, GPIO_PIN_8},
        {0,0}
    };
    printf("  [RELAYS] ");
    for (int i = 0; pins[i].port; i++) {
        printf("%s=%d ", pin_name(pins[i].port, pins[i].pin),
               sim_pin_read(pins[i].port, pins[i].pin));
    }
    printf("\n");
}

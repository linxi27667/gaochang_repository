#ifndef SIM_STM32F4XX_HAL_H
#define SIM_STM32F4XX_HAL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== GPIO ==================== */
typedef struct { int _; } GPIO_TypeDef;

#define GPIOA   ((GPIO_TypeDef *)0x100)
#define GPIOB   ((GPIO_TypeDef *)0x200)
#define GPIOC   ((GPIO_TypeDef *)0x300)

#define GPIO_PIN_0   ((uint16_t)0x0001)
#define GPIO_PIN_1   ((uint16_t)0x0002)
#define GPIO_PIN_2   ((uint16_t)0x0004)
#define GPIO_PIN_3   ((uint16_t)0x0008)
#define GPIO_PIN_4   ((uint16_t)0x0010)
#define GPIO_PIN_5   ((uint16_t)0x0020)
#define GPIO_PIN_8   ((uint16_t)0x0100)
#define GPIO_PIN_12  ((uint16_t)0x1000)
#define GPIO_PIN_13  ((uint16_t)0x2000)
#define GPIO_PIN_14  ((uint16_t)0x4000)
#define GPIO_PIN_15  ((uint16_t)0x8000)

#define GPIO_PIN_SET     1
#define GPIO_PIN_RESET   0

#define GPIO_MODE_OUTPUT_PP  0
#define GPIO_MODE_INPUT      1
#define GPIO_MODE_AF_PP      2
#define GPIO_NOPULL          0
#define GPIO_PULLUP          1
#define GPIO_SPEED_FREQ_LOW  0

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, uint8_t state);
uint8_t HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);

/* ==================== RCC ==================== */
void __HAL_RCC_GPIOA_CLK_ENABLE(void);
void __HAL_RCC_GPIOB_CLK_ENABLE(void);
void __HAL_RCC_GPIOC_CLK_ENABLE(void);
void __HAL_RCC_GPIOH_CLK_ENABLE(void);
void __HAL_RCC_TIM2_CLK_ENABLE(void);
void __HAL_RCC_SPI1_CLK_ENABLE(void);
void __HAL_RCC_USART1_CLK_ENABLE(void);

/* ==================== SysTick ==================== */
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
void sim_tick_advance(uint32_t ms);  /* 模拟时间推进 */

/* ==================== TIM ==================== */
typedef struct {
    void *Instance;
    uint32_t Channel;
} TIM_HandleTypeDef;

extern TIM_HandleTypeDef htim2;

typedef enum {
    HAL_TIM_ACTIVE_CHANNEL_1 = 0,
    HAL_TIM_ACTIVE_CHANNEL_2 = 1,
    HAL_TIM_ACTIVE_CHANNEL_CLEARED = 2,
} HAL_TIM_ActiveChannel;

#define TIM_CHANNEL_1  0
#define TIM_CHANNEL_2  1
#define TIM2           ((void *)0x40000000)
#define TIM6           ((void *)0x40001000)

typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;

HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *htim, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_IC_Init(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *htim, void *cfg);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *htim, void *cfg, uint32_t ch);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* ==================== 中断模拟 ==================== */
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t primask);

/* ==================== 编码器脉冲注入（测试用） ==================== */
void sim_encoder_pulse(uint8_t channel);

/* ==================== 按键注入（测试用） ==================== */
void sim_button_press(uint16_t pin);
void sim_button_release(uint16_t pin);

/* ==================== 引脚状态查询（测试用） ==================== */
uint8_t sim_pin_read(GPIO_TypeDef *port, uint16_t pin);
void sim_pin_print_all(void);

#endif

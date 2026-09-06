/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   GPIO configuration for GC-Screw_Lift board
  ******************************************************************************
  */
/* USER CODE END Header */
#include "gpio.h"
#include "safety.h"

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();

  /* 安全默认：LED/OUT/RELAY 全关 */
  HAL_GPIO_WritePin(GPIOC, LED_RUN_Pin|OUT3_Pin|OUT2_Pin|OUT1_Pin
                          |OUT0_Pin|RELAY4_Pin|RELAY5_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, RELAY0_Pin|RELAY1_Pin|RELAY2_Pin|RELAY3_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = LED_RUN_Pin|OUT3_Pin|OUT2_Pin|OUT1_Pin
                          |OUT0_Pin|RELAY4_Pin|RELAY5_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* IN0..IN2：按键，普通输入 */
  GPIO_InitStruct.Pin = IN0_Pin|IN1_Pin|IN2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* IN3..IN6：防撞（光耦低有效）→ 下降沿 EXTI，对齐 F407 即时停机路径 */
  GPIO_InitStruct.Pin = IN3_Pin|IN4_Pin|IN5_Pin|IN6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* PA4→EXTI4；PA5/6/7→EXTI9_5。ISR 内禁止调用 FreeRTOS API */
  HAL_NVIC_SetPriority(EXTI4_IRQn, 4, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 4, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  GPIO_InitStruct.Pin = IN7_Pin|IN8_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = IN9_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(IN9_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = RELAY0_Pin|RELAY1_Pin|RELAY2_Pin|RELAY3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 编码器脚 PB8/PB9 由 TIM4 MSP 配置为输入捕获；此处预置为浮空输入 */
  GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  Safety_EXTI_Handler(GPIO_Pin);
}

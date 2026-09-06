/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    tim.c
  * @brief   TIM4 stub（与 F407 版对齐：小剪/超薄无编码器功能）
  ******************************************************************************
  */
/* USER CODE END Header */
#include "tim.h"

TIM_HandleTypeDef htim4;

/* F407 版无 tim.c/编码器；保留空初始化以匹配 tim.h 接口 */
void MX_TIM4_Init(void)
{
}

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim)
{
    (void)htim;
}

/*
 * drv_tim.h - TIM5 输入捕获声明
 */
#ifndef DRV_TIM_H
#define DRV_TIM_H

#include "stm32f4xx_hal.h"

extern TIM_HandleTypeDef htim5;

void Drv_TIM5_Init(void);

#endif /* DRV_TIM_H */

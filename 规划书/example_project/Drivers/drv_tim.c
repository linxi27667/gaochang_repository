/*
 * drv_tim.c - TIM5 输入捕获初始化（霍尔脉冲计数）
 *
 * PA0 = TIM5_CH1 → 柱1 霍尔接近开关
 * PA1 = TIM5_CH2 → 柱2 霍尔接近开关
 *
 * 中断中通过 xTaskNotifyFromISR() 唤醒传感器任务，不在 ISR 里做业务逻辑
 */
#include "drv_tim.h"
#include "FreeRTOS.h"
#include "task.h"

TIM_HandleTypeDef htim5;

extern TaskHandle_t hSensorTask;

void Drv_TIM5_Init(void) {
    TIM_IC_InitTypeDef sConfigIC = {0};

    __HAL_RCC_TIM5_CLK_ENABLE();

    htim5.Instance = TIM5;
    htim5.Init.Prescaler = 239;          /* 240MHz / 240 = 1MHz tick */
    htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim5.Init.Period = 0xFFFF;
    HAL_TIM_IC_Init(&htim5);

    /* CH1 - PA0 柱1 */
    sConfigIC.ICPolarity = TIM_ICPOLARITY_RISING;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter = 0x06;            /* 硬件滤波，防抖动 */
    HAL_TIM_IC_ConfigChannel(&htim5, &sConfigIC, TIM_CHANNEL_1);

    /* CH2 - PA1 柱2 */
    HAL_TIM_IC_ConfigChannel(&htim5, &sConfigIC, TIM_CHANNEL_2);

    HAL_TIM_IC_Start_IT(&htim5, TIM_CHANNEL_1);
    HAL_TIM_IC_Start_IT(&htim5, TIM_CHANNEL_2);
}

/* TIM5 中断服务函数 */
void TIM5_IRQHandler(void) {
    HAL_TIM_IRQHandler(&htim5);
}

/* 输入捕获回调 — 只发通知，不做业务逻辑 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    BaseType_t hpwp = pdFALSE;

    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        /* 柱1 收到脉冲 → 通知传感器任务，值=1 */
        xTaskNotifyFromISR(hSensorTask, 1, eSetValueWithOverwrite, &hpwp);
    } else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
        /* 柱2 收到脉冲 → 通知传感器任务，值=2 */
        xTaskNotifyFromISR(hSensorTask, 2, eSetValueWithOverwrite, &hpwp);
    }
    portYIELD_FROM_ISR(hpwp);
}

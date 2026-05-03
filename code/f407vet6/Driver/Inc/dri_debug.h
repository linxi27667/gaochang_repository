#ifndef __DRI_DEBUG_H__
#define __DRI_DEBUG_H__

#include "main.h"
#include "gpio.h"
#include "app_w25qxx.h"
#include "app.h"

/* ============ 硬件配置 ============ */
#define LED_DEBUG_PORT      GPIOB
#define LED_DEBUG_PIN       GPIO_PIN_2
#define LED_BLINK_MS        100

/* ============ 对外接口 ============ */

// 创建 Debug 任务
void Debug_Task_Create(void);

// 获取当前计数器值
uint32_t Counter_Get(void);

#endif /* __DRI_DEBUG_H__ */

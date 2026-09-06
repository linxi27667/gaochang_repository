#ifndef __DRI_LIFT_H__
#define __DRI_LIFT_H__

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  举升机控制任务（替代 Control_Task）
 *         1ms 周期，依次调用 LiftCore_Poll + LiftIot_Poll + LED 心跳
 */
void Lift_Task(void *pvParameters);
extern volatile uint8_t g_lift_task_created;
extern volatile uint8_t g_lift_task_started;
extern volatile uint32_t g_lift_task_loop_cnt;

/**
 * @brief  创建举升机控制任务
 *         栈大小 768 words (3072 字节)，优先级 tskIDLE_PRIORITY + 3
 */
void Lift_Task_Create(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRI_LIFT_H__ */

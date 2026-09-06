#ifndef __DRI_LIFT_H__
#define __DRI_LIFT_H__

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

void Lift_Task(void *pvParameters);
extern volatile uint8_t g_lift_task_created;
extern volatile uint8_t g_lift_task_started;
extern volatile uint32_t g_lift_task_loop_cnt;
void Lift_Task_Create(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRI_LIFT_H__ */

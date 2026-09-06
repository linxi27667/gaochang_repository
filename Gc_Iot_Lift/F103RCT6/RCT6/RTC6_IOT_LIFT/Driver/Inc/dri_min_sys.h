#ifndef DRI_MIN_SYS_H
#define DRI_MIN_SYS_H

#include <stdint.h>
#include "FreeRTOS.h"

#define MIN_SYS_TASK_STACK_WORDS        512U
#define MIN_SYS_TASK_PRIORITY           (tskIDLE_PRIORITY + 2U)
#define MIN_SYS_TASK_PERIOD_MS          100U
#define MIN_SYS_LED_PERIOD_MS           500U
#define MIN_SYS_HEARTBEAT_PERIOD_MS    5000U
#define MIN_SYS_OUTPUT_TEST_ENABLE        1U
#define MIN_SYS_OUTPUT_TEST_ON_MS     10000U
#define MIN_SYS_OUTPUT_TEST_OFF_MS    10000U

extern volatile uint8_t g_min_sys_task_created;
extern volatile uint8_t g_min_sys_task_started;
extern volatile uint32_t g_min_sys_task_loop_count;

BaseType_t MinSys_Task_Create(void);
void MinSys_Task(void *pv_parameters);

#endif

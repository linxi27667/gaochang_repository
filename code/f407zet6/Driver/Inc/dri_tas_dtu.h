#ifndef DRI_TAS_DTU_H
#define DRI_TAS_DTU_H

#include "FreeRTOS.h"
#include "task.h"

#define TAS_DTU_TASK_STACK_SIZE_WORDS      1024U
#define TAS_DTU_TASK_PRIORITY              (tskIDLE_PRIORITY + 2U)
#define TAS_DTU_REPORT_PERIOD_MS           5000U
#define TAS_DTU_RX_POLL_PERIOD_MS          20U
#define TAS_DTU_RETRY_PERIOD_MS            60000U

void TasDtu_Task(void *pvParameters);
void TasDtu_Task_Create(void);

#endif

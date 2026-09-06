#ifndef DRI_TAS_DTU_H
#define DRI_TAS_DTU_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

/* F103 约束：heap=16384，DTU 栈 2048 words（与丝杆基建一致） */
#define TAS_DTU_TASK_STACK_SIZE_WORDS      2048U
#define TAS_DTU_TASK_PRIORITY              (tskIDLE_PRIORITY + 1U)
#define TAS_DTU_REPORT_PERIOD_MS           5000U
#define TAS_DTU_REPORT_PERIOD_MOTION_MS    1000U
#define TAS_DTU_MIN_EVENT_GAP_MS           1000U
#define TAS_DTU_RX_REPORT_GUARD_MS         1500U
#define TAS_DTU_RX_POLL_PERIOD_MS          20U
#define TAS_DTU_ERROR_RX_POLL_PERIOD_MS    1000U
#define TAS_DTU_RETRY_PERIOD_MS            60000U
#define TAS_DTU_ERROR_RETRY_PERIOD_MS      300000U

void TasDtu_Task(void *pvParameters);
extern volatile uint8_t g_tas_dtu_task_created;
void TasDtu_Task_Create(void);

#endif

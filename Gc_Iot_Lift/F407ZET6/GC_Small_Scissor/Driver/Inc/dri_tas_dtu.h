#ifndef DRI_TAS_DTU_H
#define DRI_TAS_DTU_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define TAS_DTU_MIN_TOTAL_HEAP_SIZE_BYTES 32768U

#ifndef APP_FREERTOS_TOTAL_HEAP_SIZE_BYTES
#error "APP_FREERTOS_TOTAL_HEAP_SIZE_BYTES must be defined in FreeRTOSConfig.h USER CODE Defines"
#elif (APP_FREERTOS_TOTAL_HEAP_SIZE_BYTES < TAS_DTU_MIN_TOTAL_HEAP_SIZE_BYTES)
#error "FreeRTOS heap is too small for TAS DTU task; set APP_FREERTOS_TOTAL_HEAP_SIZE_BYTES to at least 32768"
#endif

typedef char tas_dtu_heap_size_guard[
    (configTOTAL_HEAP_SIZE >= ((size_t)TAS_DTU_MIN_TOTAL_HEAP_SIZE_BYTES)) ? 1 : -1];

/* 栈扩大到 4096 words = 16384 字节：
 * 实测 1536 words 仍然溢出，导致 g_lift_task_loop_cnt 被踩踏（196→184→172→160 每次 -12）。
 * 根因：App_TasDtu_ReportConfigResult/ReportTelemetry 中 char json[1024] + snprintf 嵌套 +
 * App_TasDtu_ApplyProductTypeCommand 中 product_type[24]+device[48]+device_id[48]+
 * password[32]+account[32]+msg_id[48]+cmd_name[32]+column[16]+direction[16] 等大量局部变量，
 * 深嵌套调用下栈消耗远超 6KB。扩到 16KB 彻底解决溢出问题。
 */
#define TAS_DTU_TASK_STACK_SIZE_WORDS      4096U
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
void TasDtu_Task_Create(void);

extern volatile uint8_t g_tas_dtu_task_created;

#endif

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

/* 鏍堟墿澶у埌 4096 words = 16384 瀛楄妭锛? * 瀹炴祴 1536 words 浠嶇劧婧㈠嚭锛屽鑷?g_lift_task_loop_cnt 琚俯韪忥紙196鈫?84鈫?72鈫?60 姣忔 -12锛夈€? * 鏍瑰洜锛欰pp_TasDtu_ReportConfigResult/ReportTelemetry 涓?char json[1024] + snprintf 宓屽 +
 * App_TasDtu_ApplyProductTypeCommand 涓?product_type[24]+device[48]+device_id[48]+
 * password[32]+account[32]+msg_id[48]+cmd_name[32]+column[16]+direction[16] 绛夊ぇ閲忓眬閮ㄥ彉閲忥紝
 * 娣卞祵濂楄皟鐢ㄤ笅鏍堟秷鑰楄繙瓒?6KB銆傛墿鍒?16KB 褰诲簳瑙ｅ喅婧㈠嚭闂銆? */
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

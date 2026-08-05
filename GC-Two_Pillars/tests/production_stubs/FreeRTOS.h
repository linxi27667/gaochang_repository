#ifndef FREERTOS_H
#define FREERTOS_H
#include <stdint.h>
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xFFFFFFFFU
#define tskIDLE_PRIORITY 0U
#define pdMS_TO_TICKS(ms) (ms)
#define taskSCHEDULER_NOT_STARTED 0
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#endif

#ifndef TASK_H
#define TASK_H
#include "FreeRTOS.h"
static inline int xTaskGetSchedulerState(void) { return taskSCHEDULER_NOT_STARTED; }
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }
static inline int xTaskCreate(void (*fn)(void *), const char *name, uint16_t stack,
                              void *arg, uint32_t priority, TaskHandle_t *handle)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)priority; if (handle) *handle = (TaskHandle_t)1; return pdPASS; }
static inline void xTaskNotifyGive(TaskHandle_t handle) { (void)handle; }
static inline uint32_t ulTaskNotifyTake(int clear, TickType_t wait) { (void)clear; (void)wait; return 1U; }
static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
#endif

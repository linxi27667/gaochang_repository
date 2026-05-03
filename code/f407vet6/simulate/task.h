#ifndef SIM_TASK_H
#define SIM_TASK_H

#include <stdint.h>

typedef void (*TaskFunction_t)(void *);

int xTaskCreate(TaskFunction_t pxTaskCode, const char *pcName,
                uint32_t usStackDepth, void *pvParameters,
                uint32_t uxPriority, void *pxCreatedTask);

#endif

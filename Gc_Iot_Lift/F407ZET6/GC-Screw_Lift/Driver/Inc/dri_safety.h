#ifndef DRI_SAFETY_H
#define DRI_SAFETY_H

#include "FreeRTOS.h"
#include "task.h"

void Safety_Task(void *pvParameters);
void Safety_Task_Create(void);

#endif

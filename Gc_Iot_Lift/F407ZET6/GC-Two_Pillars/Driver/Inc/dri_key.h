#ifndef DRI_KEY_H
#define DRI_KEY_H

#include "FreeRTOS.h"
#include "task.h"

void Key_Task(void *pvParameters);
void Key_Task_Create(void);

#endif

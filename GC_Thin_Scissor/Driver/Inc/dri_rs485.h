#ifndef DRI_RS485_H
#define DRI_RS485_H

#include "FreeRTOS.h"
#include "task.h"

void RS485_Task(void *pvParameters);
void RS485_Task_Create(void);

#endif

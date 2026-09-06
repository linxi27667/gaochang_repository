#ifndef DRI_MOTOR_H
#define DRI_MOTOR_H

#include "FreeRTOS.h"
#include "task.h"

void Control_Task(void *pvParameters);
void Control_Task_Create(void);

#endif

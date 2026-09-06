/**
  ******************************************************************************
  * @file    lift_lock.c
  * @brief   举升机共享变量互斥锁实现
  ******************************************************************************
  */

#include "lift_lock.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "elog.h"

/* 静态互斥锁句柄 */
static SemaphoreHandle_t s_state_mutex = NULL;
static SemaphoreHandle_t s_iot_mutex   = NULL;

void LiftLock_Init(void)
{
    if (s_state_mutex == NULL)
    {
        s_state_mutex = xSemaphoreCreateMutex();
        if (s_state_mutex == NULL)
        {
            elog_e("LOCK", "[LOCK] state mutex create failed");
        }
    }
    if (s_iot_mutex == NULL)
    {
        s_iot_mutex = xSemaphoreCreateMutex();
        if (s_iot_mutex == NULL)
        {
            elog_e("LOCK", "[LOCK] iot mutex create failed");
        }
    }
}

void LiftLock_LockState(void)
{
    if (s_state_mutex != NULL)
    {
        (void)xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
}

void LiftLock_UnlockState(void)
{
    if (s_state_mutex != NULL)
    {
        (void)xSemaphoreGive(s_state_mutex);
    }
}

void LiftLock_LockIot(void)
{
    if (s_iot_mutex != NULL)
    {
        (void)xSemaphoreTake(s_iot_mutex, portMAX_DELAY);
    }
}

void LiftLock_UnlockIot(void)
{
    if (s_iot_mutex != NULL)
    {
        (void)xSemaphoreGive(s_iot_mutex);
    }
}

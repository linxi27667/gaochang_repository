/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS tasks for GC_Thin_Scissor (F103)
  ******************************************************************************
  */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* USER CODE BEGIN Includes */
#include "elog.h"
#include "dri_lift.h"
#include "dri_tas_dtu.h"
#include "app_io_map.h"
/* USER CODE END Includes */

osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

void StartDefaultTask(void *argument);
void MX_FREERTOS_Init(void);

void MX_FREERTOS_Init(void)
{
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  Lift_Task_Create();
  if (g_lift_task_created != 0U) {
    elog_i("SYS", "[SYS] Lift task created");
  } else {
    elog_e("SYS", "[SYS] Lift task create failed");
  }
  TasDtu_Task_Create();
  if (g_tas_dtu_task_created != 0U) {
    elog_i("SYS", "[SYS] TAS DTU task created");
  } else {
    elog_e("SYS", "[SYS] TAS DTU task create failed");
  }
}

void StartDefaultTask(void *argument)
{
  (void)argument;
  for (;;)
  {
    osDelay(1000);
  }
}

void vApplicationMallocFailedHook(void)
{
  App_IO_All_Off();
  elog_e("RTOS", "[RTOS] malloc failed free=%lu min_free=%lu",
         (unsigned long)xPortGetFreeHeapSize(),
         (unsigned long)xPortGetMinimumEverFreeHeapSize());
  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  App_IO_All_Off();
  elog_e("RTOS", "[RTOS] stack overflow task=%s",
         (pcTaskName != NULL) ? pcTaskName : "?");
  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **task_buffer,
                                   StackType_t **stack_buffer,
                                   uint32_t *stack_size)
{
  static StaticTask_t idle_task_buffer;
  static StackType_t idle_stack[configMINIMAL_STACK_SIZE];

  *task_buffer = &idle_task_buffer;
  *stack_buffer = idle_stack;
  *stack_size = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **task_buffer,
                                    StackType_t **stack_buffer,
                                    uint32_t *stack_size)
{
  static StaticTask_t timer_task_buffer;
  static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];

  *task_buffer = &timer_task_buffer;
  *stack_buffer = timer_stack;
  *stack_size = configTIMER_TASK_STACK_DEPTH;
}

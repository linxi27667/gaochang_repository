/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS tasks for GC-Two_Pillars
  ******************************************************************************
  */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* USER CODE BEGIN Includes */
#include "SEGGER_RTT.h"
#include "elog.h"
#include "app_rise_counter.h"
#include "dri_lift.h"
#include "dri_tas_dtu.h"
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

  /* USER CODE BEGIN RTOS_THREADS */
  App_RiseCounter_Task_Create();
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
  /* USER CODE END RTOS_THREADS */
}

void StartDefaultTask(void *argument)
{
  (void)argument;
  for (;;)
  {
    osDelay(1000);
  }
}

/* USER CODE BEGIN Application */

void vApplicationMallocFailedHook(void)
{
  SEGGER_RTT_WriteString(0, "E/MEM [MEM] FreeRTOS malloc failed\r\n");
  taskDISABLE_INTERRUPTS();
  while (1)
  {
  }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
  (void)task;
  (void)task_name;
  SEGGER_RTT_WriteString(0, "E/MEM [MEM] FreeRTOS stack overflow\r\n");
  taskDISABLE_INTERRUPTS();
  while (1)
  {
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

/* USER CODE END Application */

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "dri_lift.h"
#include "dri_tas_dtu.h"
#include "elog.h"
#include "app_io_map.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEFAULT_TASK_STACK_SIZE_BYTES   (2048U)
#define LIFT_TASK_PERIOD_MS             (10U)
#define LIFT_RUN_LED_TOGGLE_LOOPS       (500U / LIFT_TASK_PERIOD_MS)
#define LIFT_RTT_HEARTBEAT_LOOPS        (1000U / LIFT_TASK_PERIOD_MS)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  TasDtu_Task_Create();
  if (g_tas_dtu_task_created != 0U) {
    elog_i("SYS", "[SYS] TAS DTU task created");
  } else {
    elog_e("SYS", "[SYS] TAS DTU task create failed");
  }
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  uint32_t loop_count = 0U;
  uint32_t led_count = 0U;
  uint32_t heartbeat_count = 0U;

  (void)argument;
  elog_i("SYS", "[SYS] defaultTask lift 10ms loop started");
  LED_RUN_OFF();
  /* Infinite loop */
  for(;;)
  {
    const lift_ctx_t *ctx;

    Dri_Lift_Task10ms();
    loop_count++;

    if (++led_count >= LIFT_RUN_LED_TOGGLE_LOOPS) {
      led_count = 0U;
      LED_RUN_TOGGLE();
    }

    if (++heartbeat_count >= LIFT_RTT_HEARTBEAT_LOOPS) {
      heartbeat_count = 0U;
      ctx = Dri_Lift_GetContext();
      if (ctx != NULL) {
        elog_i("LIFT",
               "[LIFT] hb loop=%lu state=%s stack_free=%lu heap=%lu min_heap=%lu led=%u in=%u/%u/%u/%u/%u/%u/%u/%u out=%u/%u/%u",
               (unsigned long)loop_count,
               App_LiftCore_StateName(ctx->current_state),
               (unsigned long)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
               (unsigned long)xPortGetFreeHeapSize(),
               (unsigned long)xPortGetMinimumEverFreeHeapSize(),
               (unsigned int)HAL_GPIO_ReadPin(Led_Run_GPIO_Port, Led_Run_Pin),
               ctx->input.pressed[LIFT_IN_UP],
               ctx->input.pressed[LIFT_IN_DOWN],
               ctx->input.pressed[LIFT_IN_LOCK],
               ctx->input.pressed[LIFT_IN_REFILL],
               ctx->input.pressed[LIFT_IN_ESTOP],
               ctx->input.pressed[LIFT_IN_PHOTO],
               ctx->input.pressed[LIFT_IN_LOWER_LIMIT],
               ctx->input.pressed[LIFT_IN_UPPER_LIMIT],
               ctx->output_actual.motor_on,
               ctx->output_actual.down_valve_on,
               ctx->output_actual.air_valve_on);
      }
    }

    osDelay(LIFT_TASK_PERIOD_MS);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

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
  elog_e("RTOS", "[RTOS] stack overflow task=%s handle=%p free=%lu min_free=%lu",
         (pcTaskName != NULL) ? pcTaskName : "?",
         (void *)xTask,
         (unsigned long)xPortGetFreeHeapSize(),
         (unsigned long)xPortGetMinimumEverFreeHeapSize());
  taskDISABLE_INTERRUPTS();
  for (;;) {
  }
}

/* USER CODE END Application */


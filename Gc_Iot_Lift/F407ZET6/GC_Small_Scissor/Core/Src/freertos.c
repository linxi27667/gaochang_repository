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
#include "app_tas_dtu.h"
#include "app_w25qxx.h"
#include "lift_iot.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEFAULT_TASK_STACK_SIZE_BYTES   (2048U)
#define LIFT_TASK_PERIOD_MS             (10U)
#define LIFT_RTT_HEARTBEAT_LOOPS        (1000U / LIFT_TASK_PERIOD_MS)
#define LIFT_RUN_LED_NORMAL_LOOPS       (500U / LIFT_TASK_PERIOD_MS)
#define LIFT_RUN_LED_MOTION_LOOPS       (100U / LIFT_TASK_PERIOD_MS)
#define LIFT_RUN_LED_ALARM_LOOPS        (200U / LIFT_TASK_PERIOD_MS)
#define LIFT_RUN_LED_LOCKED_LOOPS       (1000U / LIFT_TASK_PERIOD_MS)
#define LIFT_COM_LED_BLINK_LOOPS        (250U / LIFT_TASK_PERIOD_MS)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#ifndef LIFT_TASK_HEARTBEAT_LOG
#define LIFT_TASK_HEARTBEAT_LOG 1
#endif

#if LIFT_TASK_HEARTBEAT_LOG == 1
#define LIFT_HEARTBEAT_LOG_I(...)  elog_i("LIFT", __VA_ARGS__)
#else
#define LIFT_HEARTBEAT_LOG_I(...)
#endif

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
static uint8_t LiftTask_IsMotionState(lift_state_t state);
static uint32_t LiftTask_RunLedPeriodLoops(const lift_ctx_t *ctx);
static void LiftTask_UpdateComLed(uint32_t *com_led_count);

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
  uint32_t run_led_count = 0U;
  uint32_t com_led_count = 0U;
  uint32_t heartbeat_count = 0U;

  (void)argument;
  elog_i("SYS", "[SYS] defaultTask lift 10ms loop started");
  LED_RUN_OFF();
  LED_COM_OFF();
  /* Infinite loop */
  for(;;)
  {
    const lift_ctx_t *ctx;
    uint32_t run_led_period;
    const tas_dtu_status_t *dtu_status;

    Dri_Lift_Task10ms();
    LiftIot_Poll();
    loop_count++;

    ctx = Dri_Lift_GetContext();
    run_led_period = LiftTask_RunLedPeriodLoops(ctx);
    if (++run_led_count >= run_led_period) {
      run_led_count = 0U;
      LED_RUN_TOGGLE();
    }
    LiftTask_UpdateComLed(&com_led_count);

    if (++heartbeat_count >= LIFT_RTT_HEARTBEAT_LOOPS) {
      heartbeat_count = 0U;
      if (ctx != NULL) {
        dtu_status = App_TasDtu_GetStatus();
        LIFT_HEARTBEAT_LOG_I(
            "[LIFT] hb loop=%lu state=%s dtu=%s stack_free=%lu heap=%lu min_heap=%lu led_run=%u led_com=%u in=%u/%u/%u/%u/%u/%u/%u/%u out=%u/%u/%u",
            (unsigned long)loop_count,
            App_LiftCore_StateName(ctx->current_state),
            (dtu_status != NULL) ? App_TasDtu_StateName(dtu_status->state) : "null",
            (unsigned long)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
            (unsigned long)xPortGetFreeHeapSize(),
            (unsigned long)xPortGetMinimumEverFreeHeapSize(),
            (unsigned int)HAL_GPIO_ReadPin(Led_Run_GPIO_Port, Led_Run_Pin),
            (unsigned int)HAL_GPIO_ReadPin(Led_Com_GPIO_Port, Led_Com_Pin),
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

static uint8_t LiftTask_IsMotionState(lift_state_t state)
{
  return (uint8_t)((state == STATE_RISING) ||
                   (state == STATE_DOWN_PREPARE) ||
                   (state == STATE_DOWN_HOLD_MOTOR) ||
                   (state == STATE_DROPPING) ||
                   (state == STATE_LOCKING) ||
                   (state == STATE_REFILL));
}

static uint32_t LiftTask_RunLedPeriodLoops(const lift_ctx_t *ctx)
{
  if (ctx == NULL) {
    return LIFT_RUN_LED_NORMAL_LOOPS;
  }

  if ((ctx->current_state == STATE_ESTOP) ||
      (ctx->current_state == STATE_PHOTO_ALARM) ||
      (ctx->current_state == STATE_FAULT)) {
    return LIFT_RUN_LED_ALARM_LOOPS;
  }

  if (ctx->remote_locked != 0U) {
    return LIFT_RUN_LED_LOCKED_LOOPS;
  }

  if (LiftTask_IsMotionState(ctx->current_state) != 0U) {
    return LIFT_RUN_LED_MOTION_LOOPS;
  }

  return LIFT_RUN_LED_NORMAL_LOOPS;
}

static void LiftTask_UpdateComLed(uint32_t *com_led_count)
{
  const tas_dtu_status_t *dtu_status = App_TasDtu_GetStatus();

  if (com_led_count == NULL) {
    return;
  }

  if (App_TasDtu_IsTransparentReady() != 0U) {
    *com_led_count = 0U;
    LED_COM_ON();
  } else if ((dtu_status == NULL) ||
             (dtu_status->state == TAS_DTU_STATE_OFF) ||
             (dtu_status->state == TAS_DTU_STATE_ERROR)) {
    *com_led_count = 0U;
    LED_COM_OFF();
  } else if (++(*com_led_count) >= LIFT_COM_LED_BLINK_LOOPS) {
    *com_led_count = 0U;
    LED_COM_TOGGLE();
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


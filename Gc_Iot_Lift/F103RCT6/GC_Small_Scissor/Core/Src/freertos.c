/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS tasks for GC_Small_Scissor (F103)
  ******************************************************************************
  */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* USER CODE BEGIN Includes */
#include "dri_lift.h"
#include "dri_tas_dtu.h"
#include "elog.h"
#include "app_io_map.h"
#include "app_tas_dtu.h"
#include "lift_iot.h"
/* USER CODE END Includes */

#define LIFT_TASK_PERIOD_MS             (10U)
#define LIFT_RTT_HEARTBEAT_LOOPS        (1000U / LIFT_TASK_PERIOD_MS)
#define LIFT_RUN_LED_NORMAL_LOOPS       (500U / LIFT_TASK_PERIOD_MS)
#define LIFT_RUN_LED_MOTION_LOOPS       (100U / LIFT_TASK_PERIOD_MS)
#define LIFT_RUN_LED_ALARM_LOOPS        (200U / LIFT_TASK_PERIOD_MS)
#define LIFT_RUN_LED_LOCKED_LOOPS       (1000U / LIFT_TASK_PERIOD_MS)
#define LIFT_COM_LED_BLINK_LOOPS        (250U / LIFT_TASK_PERIOD_MS)

#ifndef LIFT_TASK_HEARTBEAT_LOG
#define LIFT_TASK_HEARTBEAT_LOG 1
#endif

#if LIFT_TASK_HEARTBEAT_LOG == 1
#define LIFT_HEARTBEAT_LOG_I(...)  elog_i("LIFT", __VA_ARGS__)
#else
#define LIFT_HEARTBEAT_LOG_I(...)
#endif

osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

static uint8_t LiftTask_IsMotionState(lift_state_t state);
static uint32_t LiftTask_RunLedPeriodLoops(const lift_ctx_t *ctx);
static void LiftTask_UpdateComLed(uint32_t *com_led_count);

void StartDefaultTask(void *argument);
void MX_FREERTOS_Init(void);

void MX_FREERTOS_Init(void)
{
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  TasDtu_Task_Create();
  if (g_tas_dtu_task_created != 0U) {
    elog_i("SYS", "[SYS] TAS DTU task created");
  } else {
    elog_e("SYS", "[SYS] TAS DTU task create failed");
  }
}

void StartDefaultTask(void *argument)
{
  uint32_t loop_count = 0U;
  uint32_t run_led_count = 0U;
  uint32_t com_led_count = 0U;
  uint32_t heartbeat_count = 0U;

  (void)argument;
  elog_i("SYS", "[SYS] defaultTask lift 10ms loop started");
  LED_RUN_OFF();
  LED_COM_OFF();

  for (;;)
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
            "[LIFT] hb loop=%lu state=%s dtu=%s heap=%lu min_heap=%lu in=%u/%u/%u/%u/%u/%u/%u/%u out=%u/%u/%u",
            (unsigned long)loop_count,
            App_LiftCore_StateName(ctx->current_state),
            (dtu_status != NULL) ? App_TasDtu_StateName(dtu_status->state) : "null",
            (unsigned long)xPortGetFreeHeapSize(),
            (unsigned long)xPortGetMinimumEverFreeHeapSize(),
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
}

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

/* RCT6 无独立 COM LED 硬件：宏为空操作，保留与 F407 相同的状态机调用路径 */
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

#include "dri_lift.h"
#include "main.h"
#include "lift_core.h"
#include "lift_iot.h"
#include "lift_lock.h"
#include "app_io_map.h"
#include "app_product.h"
#include "app_tas_dtu.h"
#include "elog.h"

#if LIFT_CORE_DEBUG == 1
#define LIFT_LOG_I(...)  elog_i("LIFT", __VA_ARGS__)
#else
#define LIFT_LOG_I(...)
#endif

/* 任务栈大小：1024 words = 4096 字节（IoT JSON 构造较大，预留余量） */
#define LIFT_TASK_STACK_SIZE    1024
#define LIFT_TASK_PRIORITY      (tskIDLE_PRIORITY + 3)
#define LIFT_TASK_PERIOD_MS     10

/* LED 心跳周期（10ms 单位） */
#define LED_HEARTBEAT_NORMAL_CNT    50   /* 500ms 慢闪：正常静止 */
#define LED_HEARTBEAT_MOTION_CNT    10   /* 100ms 快闪：运动中 */
#define LED_HEARTBEAT_ALARM_CNT     20   /* 200ms 紧急闪：急停/光电报警 */
#define LED_HEARTBEAT_LOCKED_CNT   100   /* 1.0s 慢闪：远程锁定 */
#define LED_COM_BLINK_CNT           25   /* 250ms 快闪：DTU 连接中 */

/* 诊断标志 */
volatile uint8_t g_lift_task_created = 0;
volatile uint8_t g_lift_task_started = 0;
volatile uint8_t g_lift_task_loop_cnt = 0;
static uint32_t g_lift_led_toggle_cnt = 0;

/* 内部：举升机控制状态简表（用于 LED 模式选择，避免在 LED 处理中持锁） */
static inline uint8_t Lift_IsMotionState(lift_state_t s)
{
    return (s == LIFT_STATE_RISING || s == LIFT_STATE_DROPPING || s == LIFT_STATE_REFILLING) ? 1U : 0U;
}

void Lift_Task_Create(void)
{
    size_t free_before = xPortGetFreeHeapSize();
    size_t min_before = xPortGetMinimumEverFreeHeapSize();
    elog_i("LIFT", "[LIFT] create stack_words=%lu free_before=%lu min_free=%lu",
           (unsigned long)LIFT_TASK_STACK_SIZE,
           (unsigned long)free_before,
           (unsigned long)min_before);

    BaseType_t ret = xTaskCreate(Lift_Task, "lift", LIFT_TASK_STACK_SIZE, NULL, LIFT_TASK_PRIORITY, NULL);
    g_lift_task_created = (ret == pdPASS) ? 1 : 0;
    if (ret != pdPASS)
    {
        elog_e("LIFT", "[LIFT] task create failed ret=%ld free_after=%lu min_free=%lu",
               (long)ret,
               (unsigned long)xPortGetFreeHeapSize(),
               (unsigned long)xPortGetMinimumEverFreeHeapSize());
    }
    else
    {
        elog_i("LIFT", "[LIFT] task create ok free_after=%lu min_free=%lu",
               (unsigned long)xPortGetFreeHeapSize(),
               (unsigned long)xPortGetMinimumEverFreeHeapSize());
    }
}

void Lift_Task(void *pvParameters)
{
    (void)pvParameters;
    static uint32_t run_led_cnt = 0;
    static uint32_t com_led_cnt = 0;
    static uint32_t heartbeat_cnt = 0;
    static lift_state_t last_state = LIFT_STATE_IDLE;
    TickType_t last_wake_tick = xTaskGetTickCount();

    g_lift_task_started = 1;        /* 任务入口已执行 */
    LED_RUN_OFF();                  /* 初始为灭，进入主循环后按状态翻转 */
    LED_COM_OFF();
    g_lift_led_toggle_cnt++;

    LIFT_LOG_I("[LIFT] task started, product_type=%d (%s)",
               g_product_type, App_Product_TypeName(g_product_type));

    while (1)
    {
        App_IO_PollInputs();

        /* 1. 控制框架：急停/光电全局保护 + 产品 ops->poll() */
        LiftCore_Poll();

        /* 2. IoT 状态机：运动统计、事件标志、远程锁定强制断开 */
        LiftIot_Poll();

        /* 3. 读取快照用于 LED/日志（不持锁长时操作） */
        lift_state_snapshot_t state_snap;
        lift_iot_snapshot_t  iot_snap;
        LiftIot_Snapshot(&state_snap, &iot_snap);

        /* 4. 状态变化日志 */
        if (state_snap.state != last_state)
        {
            LIFT_LOG_I("[LIFT] state %s -> %s",
                       LiftCore_StateName(last_state),
                       LiftCore_StateName(state_snap.state));
            last_state = state_snap.state;
        }

        /* 5. LED_RUN 多码态：按安全优先级选择闪烁模式
         *    优先级：ESTOP/PHOTO_ALARM > REMOTE_LOCKED > 运动中 > 正常静止
         *    电源灯 LED_POWER 由 main.c 启动后常亮，不在本任务控制 */
        uint32_t run_period;
        if (state_snap.state == LIFT_STATE_ESTOP ||
            state_snap.state == LIFT_STATE_PHOTO_ALARM)
        {
            run_period = LED_HEARTBEAT_ALARM_CNT;     /* 200ms 紧急闪 */
        }
        else if (state_snap.remote_locked != 0U)
        {
            run_period = LED_HEARTBEAT_LOCKED_CNT;    /* 1s 慢闪 */
        }
        else if (Lift_IsMotionState(state_snap.state) != 0U)
        {
            run_period = LED_HEARTBEAT_MOTION_CNT;    /* 100ms 快闪 */
        }
        else
        {
            run_period = LED_HEARTBEAT_NORMAL_CNT;    /* 500ms 心跳 */
        }

        if (++run_led_cnt >= run_period)
        {
            run_led_cnt = 0;
            LED_RUN_TOGGLE();
            g_lift_led_toggle_cnt++;
        }

        /* 6. LED_COM：DTU 链路状态（透明传输已就绪常亮；连接中快闪；错误/关闭常灭） */
        const tas_dtu_status_t *dtu_status = App_TasDtu_GetStatus();
        if (App_TasDtu_IsTransparentReady() != 0U)
        {
            com_led_cnt = 0;
            LED_COM_ON();
        }
        else if ((dtu_status == NULL) ||
                 (dtu_status->state == TAS_DTU_STATE_OFF) ||
                 (dtu_status->state == TAS_DTU_STATE_ERROR))
        {
            com_led_cnt = 0;
            LED_COM_OFF();
        }
        else if (++com_led_cnt >= LED_COM_BLINK_CNT)
        {
            com_led_cnt = 0;
            LED_COM_TOGGLE();
        }

        g_lift_task_loop_cnt++;  /* 诊断：循环计数 */

        /* 7. 心跳日志：每 500 次循环（5 秒）打印一次 */
        if (++heartbeat_cnt >= 500U)
        {
            heartbeat_cnt = 0U;
            elog_i("LIFT", "[LIFT] hb loop=%lu state=%d dtu=%s led_tog=%lu led=%d",
                   (unsigned long)g_lift_task_loop_cnt,
                   (int)state_snap.state,
                   App_TasDtu_StateName(dtu_status->state),
                   (unsigned long)g_lift_led_toggle_cnt,
                   (int)HAL_GPIO_ReadPin(Led_Run_GPIO_Port, Led_Run_Pin));
        }

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(LIFT_TASK_PERIOD_MS));
    }
}

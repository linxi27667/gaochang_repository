#include "dri_tas_dtu.h"
#include "app_tas_dtu.h"
#include "lift_iot.h"
#include "elog.h"

/* ================= 1. DTU 任务对象与周期状态 ================= */

typedef struct
{
    uint32_t last_report_tick;
    uint32_t last_retry_tick;
} tas_dtu_task_ctx_t;

static tas_dtu_task_ctx_t g_tas_dtu_task;
volatile uint8_t g_tas_dtu_task_created = 0U;

/* ================= 2. 任务层逻辑函数 ================= */

static void TasDtu_Task_InitModule(void)
{
    vTaskDelay(pdMS_TO_TICKS(3000U));

    if (App_TasDtu_Init() != TAS_DTU_RESULT_OK)
    {
        elog_e("DTU", "[DTU] USART3 BSP init failed");
        vTaskDelete(NULL);
    }

    elog_a("DTU", "[DTU] USART3 ready at fixed 9600, starting MQTT %s:%u",
           TAS_DTU_BROKER_HOST,
           (unsigned int)TAS_DTU_BROKER_PORT);

    if (App_TasDtu_StartMqtt() != TAS_DTU_RESULT_OK)
    {
        elog_e("DTU", "[DTU] MQTT start failed");
    }
    else
    {
        elog_a("DTU", "[DTU] MQTT configured pub=%s sub=%s",
               TAS_DTU_TOPIC_TELEMETRY,
               TAS_DTU_TOPIC_COMMAND_SUB);
        (void)App_TasDtu_ReportStatus("boot");
    }

    g_tas_dtu_task.last_report_tick = HAL_GetTick();
    g_tas_dtu_task.last_retry_tick = g_tas_dtu_task.last_report_tick;
}

static void TasDtu_Task_ProcessRx(void)
{
    App_TasDtu_ProcessRx();
}

static uint8_t TasDtu_Task_IsErrorState(const tas_dtu_status_t *status)
{
    return ((status != NULL) && (status->state == TAS_DTU_STATE_ERROR)) ? 1U : 0U;
}

static void TasDtu_Task_ReportIfDue(uint32_t now)
{
    const tas_dtu_status_t *status = App_TasDtu_GetStatus();

    if (TasDtu_Task_IsErrorState(status) != 0U)
    {
        return;
    }

    /* 1. 事件即时上报：运动启停/报警变化 → 完整 telemetry */
    if (LiftIot_PeekEventFlag() != 0U)
    {
        if ((now - g_tas_dtu_task.last_report_tick) >= TAS_DTU_MIN_EVENT_GAP_MS)
        {
            (void)LiftIot_ConsumeEventFlag();
            g_tas_dtu_task.last_report_tick = now;

            if (App_TasDtu_IsTransparentReady() == 0U)
            {
                elog_w("DTU", "[DTU] event report skipped: not ready state=%s",
                       App_TasDtu_StateName(status->state));
                return;
            }

            if (App_TasDtu_ReportTelemetry() != TAS_DTU_RESULT_OK)
            {
                elog_w("DTU", "[DTU] event telemetry failed");
            }

            return;
        }
    }

    /* 2. 周期上报：统一 1000ms 一包，运动中轻量高度，静止完整 telemetry */
    uint32_t period = LiftIot_IsInMotion()
                      ? TAS_DTU_REPORT_PERIOD_MOTION_MS
                      : TAS_DTU_REPORT_PERIOD_MS;

    if ((now - g_tas_dtu_task.last_report_tick) < period)
    {
        return;
    }

    g_tas_dtu_task.last_report_tick = now;

    if (App_TasDtu_IsTransparentReady() == 0U)
    {
        elog_w("DTU", "[DTU] telemetry skipped: not ready state=%s",
               App_TasDtu_StateName(status->state));
        return;
    }

    if (LiftIot_IsInMotion())
    {
        /* 运动中：轻量高度 JSON（~160 字节） */
        if (App_TasDtu_ReportHeight() != TAS_DTU_RESULT_OK)
        {
            elog_w("DTU", "[DTU] height report failed");
        }
    }
    else
    {
        /* 静止：完整 telemetry */
        if (App_TasDtu_ReportTelemetry() != TAS_DTU_RESULT_OK)
        {
            elog_w("DTU", "[DTU] telemetry send failed");
        }
    }
}

static void TasDtu_Task_RecoverIfDue(uint32_t now)
{
    const tas_dtu_status_t *status;
    uint32_t retry_period;

    if (App_TasDtu_IsTransparentReady() != 0U)
    {
        return;
    }

    status = App_TasDtu_GetStatus();
    retry_period = (TasDtu_Task_IsErrorState(status) != 0U)
                   ? TAS_DTU_ERROR_RETRY_PERIOD_MS
                   : TAS_DTU_RETRY_PERIOD_MS;

    if ((now - g_tas_dtu_task.last_retry_tick) < retry_period)
    {
        return;
    }

    g_tas_dtu_task.last_retry_tick = now;
    elog_w("DTU", "[DTU] MQTT not ready, retry start");
    if (App_TasDtu_StartMqtt() != TAS_DTU_RESULT_OK)
    {
        elog_e("DTU", "[DTU] MQTT retry failed");
    }
    else
    {
        elog_a("DTU", "[DTU] MQTT retry recovered");
        (void)App_TasDtu_ReportStatus("recovered");
    }
}

/* ================= 3. FreeRTOS 任务创建与调度 ================= */

void TasDtu_Task_Create(void)
{
    BaseType_t ret;

    elog_i("DTU", "[DTU] create stack_words=%lu free_before=%lu min_free=%lu",
           (unsigned long)TAS_DTU_TASK_STACK_SIZE_WORDS,
           (unsigned long)xPortGetFreeHeapSize(),
           (unsigned long)xPortGetMinimumEverFreeHeapSize());

    ret = xTaskCreate(TasDtu_Task,
                      "tas_dtu",
                      TAS_DTU_TASK_STACK_SIZE_WORDS,
                      NULL,
                      TAS_DTU_TASK_PRIORITY,
                      NULL);

    g_tas_dtu_task_created = (ret == pdPASS) ? 1U : 0U;
    if (ret != pdPASS)
    {
        elog_e("DTU", "[DTU] task create failed ret=%ld free_after=%lu min_free=%lu",
               (long)ret,
               (unsigned long)xPortGetFreeHeapSize(),
               (unsigned long)xPortGetMinimumEverFreeHeapSize());
    }
    else
    {
        elog_i("DTU", "[DTU] task create ok free_after=%lu min_free=%lu",
               (unsigned long)xPortGetFreeHeapSize(),
               (unsigned long)xPortGetMinimumEverFreeHeapSize());
    }
}

void TasDtu_Task(void *pvParameters)
{
    (void)pvParameters;

    TasDtu_Task_InitModule();

    while (1)
    {
        uint32_t now = HAL_GetTick();
        const tas_dtu_status_t *status;
        uint32_t poll_period_ms;

        TasDtu_Task_ProcessRx();
        TasDtu_Task_RecoverIfDue(now);
        TasDtu_Task_ReportIfDue(now);

        status = App_TasDtu_GetStatus();
        poll_period_ms = (TasDtu_Task_IsErrorState(status) != 0U)
                         ? TAS_DTU_ERROR_RX_POLL_PERIOD_MS
                         : TAS_DTU_RX_POLL_PERIOD_MS;

        vTaskDelay(pdMS_TO_TICKS(poll_period_ms));
    }
}

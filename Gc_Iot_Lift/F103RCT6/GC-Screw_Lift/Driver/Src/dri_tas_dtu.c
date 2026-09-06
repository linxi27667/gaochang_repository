#include "dri_tas_dtu.h"
#include "app_tas_dtu.h"
#include "app_lift_iot.h"
#include "cmsis_os.h"
#include "elog.h"

/* ================= 1. DTU 任务对象与周期状态 ================= */

typedef struct
{
    uint32_t last_report_tick;
    uint32_t last_retry_tick;
} tas_dtu_task_ctx_t;

static tas_dtu_task_ctx_t g_tas_dtu_task;

/* ================= 2. 任务层逻辑函数 ================= */

static void TasDtu_Task_InitModule(void)
{
    osDelay(3000U);

    if (App_TasDtu_Init() != TAS_DTU_RESULT_OK)
    {
        elog_e("DTU", "[DTU] USART1 BSP init failed");
        vTaskDelete(NULL);
    }

    elog_a("DTU", "[DTU] USART1 ready at fixed 9600, starting MQTT %s:%u",
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

static void TasDtu_Task_ReportIfDue(uint32_t now)
{
    /* 1. 事件即时上报：运动启停/报警变化 → 完整 telemetry */
    if (App_LiftIot_PeekEventFlag() != 0U)
    {
        if ((now - g_tas_dtu_task.last_report_tick) >= TAS_DTU_MIN_EVENT_GAP_MS)
        {
            (void)App_LiftIot_ConsumeEventFlag();
            g_tas_dtu_task.last_report_tick = now;

            if (App_TasDtu_IsTransparentReady() == 0U)
            {
                const tas_dtu_status_t *status = App_TasDtu_GetStatus();
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

    /* 2. 周期上报：运动中 500ms 轻量高度，静止 5000ms 完整 telemetry */
    uint32_t period = App_LiftIot_IsInMotion()
                      ? TAS_DTU_REPORT_PERIOD_MOTION_MS
                      : TAS_DTU_REPORT_PERIOD_MS;

    if ((now - g_tas_dtu_task.last_report_tick) < period)
    {
        return;
    }

    g_tas_dtu_task.last_report_tick = now;

    if (App_TasDtu_IsTransparentReady() == 0U)
    {
        const tas_dtu_status_t *status = App_TasDtu_GetStatus();
        elog_w("DTU", "[DTU] telemetry skipped: not ready state=%s",
               App_TasDtu_StateName(status->state));
        return;
    }

    if (App_LiftIot_IsInMotion())
    {
        /* 运动中：轻量高度与当前安全状态 JSON */
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
    if (App_TasDtu_IsTransparentReady() != 0U)
    {
        return;
    }

    if ((now - g_tas_dtu_task.last_retry_tick) < TAS_DTU_RETRY_PERIOD_MS)
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
    xTaskCreate(TasDtu_Task,
                "tas_dtu",
                TAS_DTU_TASK_STACK_SIZE_WORDS,
                NULL,
                TAS_DTU_TASK_PRIORITY,
                NULL);
}

void TasDtu_Task(void *pvParameters)
{
    (void)pvParameters;

    TasDtu_Task_InitModule();

    while (1)
    {
        uint32_t now = HAL_GetTick();

        TasDtu_Task_ProcessRx();
        TasDtu_Task_RecoverIfDue(now);
        TasDtu_Task_ReportIfDue(now);

        osDelay(TAS_DTU_RX_POLL_PERIOD_MS);
    }
}

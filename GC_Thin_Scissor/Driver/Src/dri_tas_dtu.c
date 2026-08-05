#include "dri_tas_dtu.h"
#include "app_tas_dtu.h"
#include "lift_iot.h"
#include "main.h"
#include "elog.h"

#ifndef TAS_DTU_CONNECT_LOG
#define TAS_DTU_CONNECT_LOG 1
#endif

#ifndef TAS_DTU_TRANSFER_LOG
#define TAS_DTU_TRANSFER_LOG 1
#endif

#if TAS_DTU_CONNECT_LOG == 1
#define DTU_CONNECT_LOG_A(...)  elog_a(__VA_ARGS__)
#define DTU_CONNECT_LOG_I(...)  elog_i(__VA_ARGS__)
#define DTU_CONNECT_LOG_W(...)  elog_w(__VA_ARGS__)
#define DTU_CONNECT_LOG_E(...)  elog_e(__VA_ARGS__)
#else
#define DTU_CONNECT_LOG_A(...)
#define DTU_CONNECT_LOG_I(...)
#define DTU_CONNECT_LOG_W(...)
#define DTU_CONNECT_LOG_E(...)
#endif

#if TAS_DTU_TRANSFER_LOG == 1
#define DTU_TRANSFER_LOG_W(...)  elog_w(__VA_ARGS__)
#else
#define DTU_TRANSFER_LOG_W(...)
#endif

/* ================= 1. DTU 浠诲姟瀵硅薄涓庡懆鏈熺姸鎬?================= */

typedef struct
{
    uint32_t last_report_tick;
    uint32_t last_retry_tick;
} tas_dtu_task_ctx_t;

static tas_dtu_task_ctx_t g_tas_dtu_task;
volatile uint8_t g_tas_dtu_task_created = 0U;

/* ================= 2. 浠诲姟灞傞€昏緫鍑芥暟 ================= */

static void TasDtu_Task_InitModule(void)
{
    vTaskDelay(pdMS_TO_TICKS(3000U));

    if (App_TasDtu_Init() != TAS_DTU_RESULT_OK)
    {
        DTU_CONNECT_LOG_E("DTU", "[DTU] USART3 BSP init failed");
        vTaskDelete(NULL);
    }

    DTU_CONNECT_LOG_A("DTU", "[DTU] USART3 ready at fixed 9600, starting MQTT %s:%u",
           TAS_DTU_BROKER_HOST,
           (unsigned int)TAS_DTU_BROKER_PORT);

    if (App_TasDtu_StartMqtt() != TAS_DTU_RESULT_OK)
    {
        DTU_CONNECT_LOG_E("DTU", "[DTU] MQTT start failed");
    }
    else
    {
        DTU_CONNECT_LOG_A("DTU", "[DTU] MQTT configured pub=%s sub=%s",
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

    /* Prioritize command parsing and its response on the 9600-baud link. */
    if ((status != NULL) && (status->last_rx_tick != 0U) &&
        ((now - status->last_rx_tick) < TAS_DTU_RX_REPORT_GUARD_MS))
    {
        return;
    }

    /* 1. 浜嬩欢鍗虫椂涓婃姤锛氳繍鍔ㄥ惎鍋?鎶ヨ鍙樺寲 鈫?瀹屾暣 telemetry */
    if (LiftIot_PeekEventFlag() != 0U)
    {
        if ((now - g_tas_dtu_task.last_report_tick) >= TAS_DTU_MIN_EVENT_GAP_MS)
        {
            (void)LiftIot_ConsumeEventFlag();
            g_tas_dtu_task.last_report_tick = now;

            if (App_TasDtu_IsTransparentReady() == 0U)
            {
                DTU_TRANSFER_LOG_W("DTU", "[DTU] event report skipped: not ready state=%s",
                       App_TasDtu_StateName(status->state));
                return;
            }

            if (App_TasDtu_ReportTelemetry() != TAS_DTU_RESULT_OK)
            {
                DTU_TRANSFER_LOG_W("DTU", "[DTU] event telemetry failed");
            }

            return;
        }
    }

    /* 2. 鍛ㄦ湡涓婃姤锛氱粺涓€ 1000ms 涓€鍖咃紝杩愬姩涓交閲忛珮搴︼紝闈欐瀹屾暣 telemetry */
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
        DTU_TRANSFER_LOG_W("DTU", "[DTU] telemetry skipped: not ready state=%s",
               App_TasDtu_StateName(status->state));
        return;
    }

    if (LiftIot_IsInMotion())
    {
        /* 杩愬姩涓細杞婚噺楂樺害 JSON锛垀160 瀛楄妭锛?*/
        if (App_TasDtu_ReportHeight() != TAS_DTU_RESULT_OK)
        {
            DTU_TRANSFER_LOG_W("DTU", "[DTU] height report failed");
        }
    }
    else
    {
        /* 闈欐锛氬畬鏁?telemetry */
        if (App_TasDtu_ReportTelemetry() != TAS_DTU_RESULT_OK)
        {
            DTU_TRANSFER_LOG_W("DTU", "[DTU] telemetry send failed");
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
    DTU_CONNECT_LOG_W("DTU", "[DTU] MQTT not ready, retry start");
    if (App_TasDtu_StartMqtt() != TAS_DTU_RESULT_OK)
    {
        DTU_CONNECT_LOG_E("DTU", "[DTU] MQTT retry failed");
    }
    else
    {
        DTU_CONNECT_LOG_A("DTU", "[DTU] MQTT retry recovered");
        (void)App_TasDtu_ReportStatus("recovered");
    }
}

/* ================= 3. FreeRTOS 浠诲姟鍒涘缓涓庤皟搴?================= */

void TasDtu_Task_Create(void)
{
    BaseType_t ret;

    DTU_CONNECT_LOG_I("DTU", "[DTU] create stack_words=%lu free_before=%lu min_free=%lu",
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
        DTU_CONNECT_LOG_E("DTU", "[DTU] task create failed ret=%ld free_after=%lu min_free=%lu",
               (long)ret,
               (unsigned long)xPortGetFreeHeapSize(),
               (unsigned long)xPortGetMinimumEverFreeHeapSize());
    }
    else
    {
        DTU_CONNECT_LOG_I("DTU", "[DTU] task create ok free_after=%lu min_free=%lu",
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

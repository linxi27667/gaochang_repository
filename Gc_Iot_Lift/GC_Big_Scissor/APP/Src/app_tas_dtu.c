#include "app_tas_dtu.h"
#include "app_maintenance.h"
#include "lift_iot.h"
#include "app_w25qxx.h"
#include "encoder.h"
#include "key.h"
#include "motor.h"
#include "safety.h"
#include "app_buzzer.h"
#include "usart.h"
#include "main.h"
#include "elog.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifndef TAS_DTU_CONNECT_LOG
#define TAS_DTU_CONNECT_LOG 1
#endif

#ifndef TAS_DTU_TRANSFER_LOG
#define TAS_DTU_TRANSFER_LOG 1
#endif

#if TAS_DTU_CONNECT_LOG == 1
#define DTU_CONNECT_LOG_A(...)  elog_a(__VA_ARGS__)
#define DTU_CONNECT_LOG_W(...)  elog_w(__VA_ARGS__)
#define DTU_CONNECT_LOG_E(...)  elog_e(__VA_ARGS__)
#else
#define DTU_CONNECT_LOG_A(...)
#define DTU_CONNECT_LOG_W(...)
#define DTU_CONNECT_LOG_E(...)
#endif

#if TAS_DTU_TRANSFER_LOG == 1
#define DTU_TRANSFER_LOG_A(...)  elog_a(__VA_ARGS__)
#define DTU_TRANSFER_LOG_W(...)  elog_w(__VA_ARGS__)
#define DTU_TRANSFER_LOG_E(...)  elog_e(__VA_ARGS__)
#else
#define DTU_TRANSFER_LOG_A(...)
#define DTU_TRANSFER_LOG_W(...)
#define DTU_TRANSFER_LOG_E(...)
#endif

/* ================= Private Objects ================= */

bsp_uart_t g_tas_dtu_uart;

static uint8_t g_tas_dtu_rx_dma_buf[TAS_DTU_RX_DMA_SIZE];
static tas_dtu_status_t g_tas_dtu_status;
static char g_tas_dtu_line[TAS_DTU_LINE_SIZE];
static uint16_t g_tas_dtu_line_len;
static int16_t g_tas_dtu_json_depth;
static char g_tas_dtu_at_resp[TAS_DTU_AT_RESPONSE_SIZE];
static uint32_t g_tas_dtu_rise_last_report_tick;

#define TAS_DTU_STM32_UID_BASE_ADDR  (0x1FFF7A10UL)
#define TAS_DTU_MSG_ID_CACHE_SIZE    (16U)

typedef struct
{
    char msg_id[48];
    char cmd[32];
    char result_text[32];
    char event[32];
} tas_dtu_cmd_cache_t;

static tas_dtu_cmd_cache_t g_tas_dtu_cmd_cache[TAS_DTU_MSG_ID_CACHE_SIZE];
static uint8_t g_tas_dtu_cmd_cache_next;
static uint8_t g_tas_dtu_saved_config_mismatch;

/* ================= Private Helpers ================= */

static const char *App_TasDtu_ResultName(tas_dtu_result_t result)
{
    switch (result)
    {
        case TAS_DTU_RESULT_OK:
            return "ok";

        case TAS_DTU_RESULT_PARAM_ERROR:
            return "param_error";

        case TAS_DTU_RESULT_UART_ERROR:
            return "uart_error";

        case TAS_DTU_RESULT_TIMEOUT:
            return "timeout";

        case TAS_DTU_RESULT_AT_ERROR:
            return "at_error";

        case TAS_DTU_RESULT_NOT_READY:
            return "not_ready";

        case TAS_DTU_RESULT_BUFFER_SMALL:
        default:
            return "buffer_small";
    }
}

static void App_TasDtu_LogAtTx(const char *cmd)
{
    if (cmd == NULL)
    {
        return;
    }

    if (strstr(cmd, "AT+USERPWD") != NULL)
    {
        DTU_TRANSFER_LOG_A("DTU", "[DTU] AT TX: AT+USERPWD=***");
        return;
    }

    DTU_TRANSFER_LOG_A("DTU", "[DTU] AT TX: %s", cmd);
}

static void App_TasDtu_LogAtRx(tas_dtu_result_t result,
                               const char *expect,
                               const char *resp)
{
    if (resp == NULL)
    {
        resp = "";
    }

    if (expect == NULL)
    {
        expect = "";
    }

    DTU_TRANSFER_LOG_A("DTU",
           "[DTU] AT RX result=%s expect=%s resp=%.160s",
           App_TasDtu_ResultName(result),
           expect,
           resp);
}

static const char *App_TasDtu_ChipUidString(void)
{
    static char uid[25];
    const volatile uint32_t *uid_reg = (const volatile uint32_t *)TAS_DTU_STM32_UID_BASE_ADDR;

    if (uid[0] == '\0')
    {
        (void)snprintf(uid, sizeof(uid), "%08lx%08lx%08lx",
                       (unsigned long)uid_reg[0],
                       (unsigned long)uid_reg[1],
                       (unsigned long)uid_reg[2]);
    }

    return uid;
}

static void App_TasDtu_BuildClientId(char *buf, uint16_t size)
{
    if ((buf == NULL) || (size == 0U))
    {
        return;
    }

    (void)snprintf(buf, size, "gc-lift-%s", App_TasDtu_ChipUidString());
}

static void App_TasDtu_BuildV1Topic(char *buf, uint16_t size, const char *suffix)
{
    if ((buf == NULL) || (size == 0U) || (suffix == NULL))
    {
        return;
    }

    (void)snprintf(buf, size, "gaochang/lift/v1/devices/%s/%s",
                   App_TasDtu_ChipUidString(),
                   suffix);
}

static void App_TasDtu_DelayMs(uint32_t ms)
{
    if (ms > 0U)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static void App_TasDtu_SetError(tas_dtu_result_t result)
{
    (void)result;
    g_tas_dtu_status.state = TAS_DTU_STATE_ERROR;
    g_tas_dtu_status.mqtt_configured = 0U;
    g_tas_dtu_status.transparent_ready = 0U;
    g_tas_dtu_status.last_error_tick = HAL_GetTick();
}

static tas_dtu_result_t App_TasDtu_SetUartBaud(uint32_t baud)
{
    if (baud == 0U)
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    if ((g_tas_dtu_status.current_baud == baud) && (huart3.Init.BaudRate == baud))
    {
        return TAS_DTU_RESULT_OK;
    }

    DTU_CONNECT_LOG_A("DTU", "[DTU] switch USART3 baud to %lu", (unsigned long)baud);

    (void)HAL_UART_AbortReceive(&huart3);
    (void)HAL_UART_AbortTransmit(&huart3);
    (void)HAL_UART_DeInit(&huart3);

    huart3.Init.BaudRate = baud;
    if (HAL_UART_Init(&huart3) != HAL_OK)
    {
        App_TasDtu_SetError(TAS_DTU_RESULT_UART_ERROR);
        return TAS_DTU_RESULT_UART_ERROR;
    }

    BSP_UART_FlushRx(&g_tas_dtu_uart);
    BSP_UART_FlushTx(&g_tas_dtu_uart);
    if (BSP_UART_RestartRx(&g_tas_dtu_uart) != BSP_UART_OK)
    {
        App_TasDtu_SetError(TAS_DTU_RESULT_UART_ERROR);
        return TAS_DTU_RESULT_UART_ERROR;
    }

    g_tas_dtu_status.current_baud = baud;
    App_TasDtu_DelayMs(200U);
    return TAS_DTU_RESULT_OK;
}

static void App_TasDtu_FlushInput(void)
{
    BSP_UART_FlushRx(&g_tas_dtu_uart);
    g_tas_dtu_line_len = 0U;
    g_tas_dtu_json_depth = 0;
    memset(g_tas_dtu_at_resp, 0, sizeof(g_tas_dtu_at_resp));
}

static tas_dtu_result_t App_TasDtu_SendRaw(const char *data)
{
    uint16_t len;

    if (data == NULL)
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    len = (uint16_t)strlen(data);
    if (BSP_UART_SendBlocking(&g_tas_dtu_uart,
                              (const uint8_t *)data,
                              len,
                              (uint32_t)len + 1000U) != BSP_UART_OK)
    {
        DTU_TRANSFER_LOG_W("DTU", "[DTU] uart blocking tx failed: len=%u", len);
        return TAS_DTU_RESULT_UART_ERROR;
    }

    g_tas_dtu_status.last_tx_tick = HAL_GetTick();
    return TAS_DTU_RESULT_OK;
}

static tas_dtu_result_t App_TasDtu_WaitTxIdle(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (BSP_UART_TxPending(&g_tas_dtu_uart) == 0U)
        {
            return TAS_DTU_RESULT_OK;
        }

        App_TasDtu_DelayMs(1U);
    }

    DTU_TRANSFER_LOG_W("DTU",
           "[DTU] uart tx wait idle timeout: pending=%u",
           BSP_UART_TxPending(&g_tas_dtu_uart));
    return TAS_DTU_RESULT_TIMEOUT;
}

static uint8_t App_TasDtu_ResponseHasError(const char *resp)
{
    if (resp == NULL)
    {
        return 0U;
    }

    if (strstr(resp, "ERROR") != NULL)
    {
        return 1U;
    }

    if (strstr(resp, "BUSY") != NULL)
    {
        return 1U;
    }

    if (strstr(resp, "NO CARRIER") != NULL)
    {
        return 1U;
    }

    if (strstr(resp, "CONNECT FAIL") != NULL)
    {
        return 1U;
    }

    return 0U;
}

static char App_TasDtu_ToLower(char ch)
{
    if ((ch >= 'A') && (ch <= 'Z'))
    {
        return (char)(ch + ('a' - 'A'));
    }

    return ch;
}

static uint8_t App_TasDtu_ContainsIgnoreCase(const char *text, const char *pattern)
{
    uint16_t text_len;
    uint16_t pattern_len;

    if ((text == NULL) || (pattern == NULL))
    {
        return 0U;
    }

    text_len = (uint16_t)strlen(text);
    pattern_len = (uint16_t)strlen(pattern);

    if ((pattern_len == 0U) || (text_len < pattern_len))
    {
        return 0U;
    }

    for (uint16_t i = 0U; i <= (uint16_t)(text_len - pattern_len); i++)
    {
        uint8_t matched = 1U;

        for (uint16_t j = 0U; j < pattern_len; j++)
        {
            if (App_TasDtu_ToLower(text[i + j]) != App_TasDtu_ToLower(pattern[j]))
            {
                matched = 0U;
                break;
            }
        }

        if (matched)
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t App_TasDtu_IsHexChar(char ch)
{
    if ((ch >= '0') && (ch <= '9'))
    {
        return 1U;
    }

    ch = App_TasDtu_ToLower(ch);
    return ((ch >= 'a') && (ch <= 'f')) ? 1U : 0U;
}

static void App_TasDtu_NormalizeUid(const char *src, char *dst, uint16_t size)
{
    uint16_t out = 0U;

    if ((dst == NULL) || (size == 0U))
    {
        return;
    }

    dst[0] = '\0';
    if (src == NULL)
    {
        return;
    }

    if ((src[0] == '0') && (App_TasDtu_ToLower(src[1]) == 'x'))
    {
        src += 2;
    }

    while ((*src != '\0') && (out < (uint16_t)(size - 1U)))
    {
        if (App_TasDtu_IsHexChar(*src) != 0U)
        {
            dst[out++] = App_TasDtu_ToLower(*src);
        }
        src++;
    }

    dst[out] = '\0';
}

static uint8_t App_TasDtu_UidMatchesLocal(const char *candidate)
{
    char local_uid[32];
    char remote_uid[64];

    App_TasDtu_NormalizeUid(App_TasDtu_ChipUidString(), local_uid, sizeof(local_uid));
    App_TasDtu_NormalizeUid(candidate, remote_uid, sizeof(remote_uid));

    if ((local_uid[0] == '\0') || (remote_uid[0] == '\0'))
    {
        return 0U;
    }

    return (strcmp(local_uid, remote_uid) == 0) ? 1U : 0U;
}

static tas_dtu_result_t App_TasDtu_WaitFor(const char *expect,
                                           uint32_t timeout_ms,
                                           char *resp,
                                           uint16_t resp_size)
{
    uint32_t start;
    uint16_t len;
    uint16_t total_rx;
    uint16_t yield_cnt;

    if ((expect == NULL) || (resp == NULL) || (resp_size < 2U))
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    resp[0] = '\0';
    len = 0U;
    total_rx = 0U;
    yield_cnt = 0U;
    start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint8_t ch;

        /* 内层循环处理 FIFO 数据。空板上 UART RX 悬空会持续收到噪声，
         * 若不让出 CPU，DTU 任务会长时间占用调度，干扰 Lift_Task 心跳。
         * 每处理 32 字节主动 taskYIELD() 一次，保证 Lift_Task 能及时调度。
         */
        while (BSP_UART_ReadByte(&g_tas_dtu_uart, &ch))
        {
            total_rx++;

            if (len < (uint16_t)(resp_size - 1U))
            {
                resp[len++] = (char)ch;
                resp[len] = '\0';
            }
            else
            {
                memmove(resp, &resp[1], (uint16_t)(resp_size - 2U));
                resp[resp_size - 2U] = (char)ch;
                resp[resp_size - 1U] = '\0';
                len = (uint16_t)(resp_size - 1U);
            }

            g_tas_dtu_status.last_rx_tick = HAL_GetTick();

            if (App_TasDtu_ContainsIgnoreCase(resp, expect))
            {
                g_tas_dtu_status.last_rx_bytes = total_rx;
                return TAS_DTU_RESULT_OK;
            }

            if (App_TasDtu_ResponseHasError(resp))
            {
                g_tas_dtu_status.at_error_count++;
                g_tas_dtu_status.last_rx_bytes = total_rx;
                return TAS_DTU_RESULT_AT_ERROR;
            }

            if (++yield_cnt >= 32U)
            {
                yield_cnt = 0U;
                taskYIELD();
            }
        }

        App_TasDtu_DelayMs(1U);
    }

    g_tas_dtu_status.last_rx_bytes = total_rx;
    g_tas_dtu_status.timeout_count++;
    return TAS_DTU_RESULT_TIMEOUT;
}

static tas_dtu_result_t App_TasDtu_SendCommand(const char *cmd,
                                               const char *expect,
                                               uint32_t timeout_ms)
{
    char tx_buf[256];
    tas_dtu_result_t result;

    if ((cmd == NULL) || (expect == NULL))
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    if (snprintf(tx_buf, sizeof(tx_buf), "%s\r\n", cmd) >= (int)sizeof(tx_buf))
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    App_TasDtu_FlushInput();
    App_TasDtu_LogAtTx(cmd);

    result = App_TasDtu_SendRaw(tx_buf);
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_LogAtRx(result, expect, "");
        return result;
    }

    (void)App_TasDtu_WaitTxIdle(2000U);

    result = App_TasDtu_WaitFor(expect,
                                timeout_ms,
                                g_tas_dtu_at_resp,
                                (uint16_t)sizeof(g_tas_dtu_at_resp));
    App_TasDtu_LogAtRx(result, expect, g_tas_dtu_at_resp);

    return result;
}

static tas_dtu_result_t App_TasDtu_TestAt(void)
{
    for (uint8_t i = 0U; i < 3U; i++)
    {
        if (App_TasDtu_SendCommand("AT", "OK", 1000U) == TAS_DTU_RESULT_OK)
        {
            return TAS_DTU_RESULT_OK;
        }

        App_TasDtu_DelayMs(300U);
    }

    return TAS_DTU_RESULT_TIMEOUT;
}

static tas_dtu_result_t App_TasDtu_EnterCommandMode(void)
{
    tas_dtu_result_t result;

    /*
     * 上电后模块通常已在透传态，先发 AT 会被当作业务数据发到 MQTT。
     * 因此固定先按手册用 +++ 进入命令模式；若 +++ 失败，再用 AT 判断是否本来就在命令模式。
     */
    (void)App_TasDtu_WaitTxIdle(2000U);
    g_tas_dtu_status.transparent_ready = 0U;
    App_TasDtu_DelayMs(1200U);

    for (uint8_t i = 0U; i < 3U; i++)
    {
        App_TasDtu_FlushInput();
        DTU_CONNECT_LOG_A("DTU", "[DTU] enter command mode: send +++ (attempt %u/3)",
               (unsigned int)(i + 1U));
        result = App_TasDtu_SendRaw("+++");
        if (result != TAS_DTU_RESULT_OK)
        {
            DTU_CONNECT_LOG_E("DTU", "[DTU] +++ send failed: %s", App_TasDtu_ResultName(result));
            continue;
        }

        (void)App_TasDtu_WaitTxIdle(1000U);
        result = App_TasDtu_WaitFor("OK", 3000U,
                                    g_tas_dtu_at_resp,
                                    (uint16_t)sizeof(g_tas_dtu_at_resp));
        if (result == TAS_DTU_RESULT_OK)
        {
            g_tas_dtu_status.state = TAS_DTU_STATE_CMD_MODE;
            DTU_CONNECT_LOG_A("DTU", "[DTU] enter command mode ok (attempt %u)",
                   (unsigned int)(i + 1U));
            return TAS_DTU_RESULT_OK;
        }

        /* +++ 没响应，再试一次 AT，判断模块是否本来就在命令模式。 */
        if (App_TasDtu_TestAt() == TAS_DTU_RESULT_OK)
        {
            g_tas_dtu_status.state = TAS_DTU_STATE_CMD_MODE;
            DTU_CONNECT_LOG_A("DTU", "[DTU] command mode already active");
            return TAS_DTU_RESULT_OK;
        }

        if (i < 2U)
        {
            App_TasDtu_DelayMs(1000U);
        }
    }

    DTU_CONNECT_LOG_E("DTU", "[DTU] enter command mode failed after 3 attempts");
    App_TasDtu_SetError(TAS_DTU_RESULT_TIMEOUT);
    return TAS_DTU_RESULT_TIMEOUT;
}

static tas_dtu_result_t App_TasDtu_SendConfigCommand(const char *cmd,
                                                     uint32_t timeout_ms)
{
    tas_dtu_result_t result = App_TasDtu_SendCommand(cmd, "OK", timeout_ms);

    if (result != TAS_DTU_RESULT_OK)
    {
        DTU_CONNECT_LOG_E("DTU", "[DTU] config command failed: %s result=%s",
               cmd,
               App_TasDtu_ResultName(result));
    }

    return result;
}

static tas_dtu_result_t App_TasDtu_SendConfigFormat(uint32_t timeout_ms,
                                                    const char *format,
                                                    ...)
{
    char cmd[384];
    va_list args;
    int len;

    if (format == NULL)
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    va_start(args, format);
    len = vsnprintf(cmd, sizeof(cmd), format, args);
    va_end(args);

    if ((len < 0) || (len >= (int)sizeof(cmd)))
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    return App_TasDtu_SendConfigCommand(cmd, timeout_ms);
}

static tas_dtu_result_t App_TasDtu_ConfigMqttChannel(void)
{
    tas_dtu_result_t result;
    char client_id[80];
    char topic_up[96];
    char topic_down[96];

    App_TasDtu_BuildClientId(client_id, sizeof(client_id));
    App_TasDtu_BuildV1Topic(topic_up, sizeof(topic_up), "up");
    App_TasDtu_BuildV1Topic(topic_down, sizeof(topic_down), "down");

    result = App_TasDtu_SendConfigCommand("AT+DTUMODE=2,1", 3000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigFormat(5000U,
                                         "AT+IPPORT=\"%s\",%u,1",
                                         TAS_DTU_BROKER_HOST,
                                         (unsigned int)TAS_DTU_BROKER_PORT);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+CLIENTID=\"%s\",1",
                                         client_id);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+USERPWD=\"%s\",\"%s\",1",
                                         TAS_DTU_MQTT_USERNAME,
                                         TAS_DTU_MQTT_PASSWORD);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+MQTTSUB=1,\"%s\",0,1,1",
                                         topic_down);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+MQTTPUB=1,\"%s\",0,0,1,1",
                                         topic_up);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigCommand("AT+MQTTPUB=0,\"\",0,0,2,1", 3000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigCommand("AT+MQTTKEEP=30,1", 3000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigCommand("AT+CLEANSESSION=1,1", 3000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigCommand("AT+BLOCKINFO=0,0", 3000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigCommand("AT+AUTOSTATUS=1,1", 3000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigCommand("AT+DTUPACKET=0,1024", 3000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigCommand("AT+RELINKTIME=30", 3000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    return App_TasDtu_SendConfigCommand("AT+DSCTIME=300", 3000U);
}

static uint8_t App_TasDtu_QueryContains(const char *cmd,
                                        const char *expected,
                                        const char *label)
{
    tas_dtu_result_t result;

    result = App_TasDtu_SendCommand(cmd, "OK", 3000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        DTU_CONNECT_LOG_W("DTU", "[DTU] saved config query failed field=%s result=%s",
                          label, App_TasDtu_ResultName(result));
        return 0U;
    }

    if (strstr(g_tas_dtu_at_resp, expected) == NULL)
    {
        DTU_CONNECT_LOG_W("DTU", "[DTU] saved config mismatch field=%s expected=%s resp=%.160s",
                          label, expected, g_tas_dtu_at_resp);
        return 0U;
    }

    return 1U;
}

static uint8_t App_TasDtu_SavedMqttConfigMatches(void)
{
    char broker[64];
    char broker_port[8];
    char client_id[80];
    char topic_up[96];
    char topic_down[96];

    (void)snprintf(broker, sizeof(broker), "%s", TAS_DTU_BROKER_HOST);
    (void)snprintf(broker_port, sizeof(broker_port), "%u", (unsigned int)TAS_DTU_BROKER_PORT);
    App_TasDtu_BuildClientId(client_id, sizeof(client_id));
    App_TasDtu_BuildV1Topic(topic_up, sizeof(topic_up), "up");
    App_TasDtu_BuildV1Topic(topic_down, sizeof(topic_down), "down");

    if (App_TasDtu_QueryContains("AT+IPPORT?", broker, "broker") == 0U ||
        strstr(g_tas_dtu_at_resp, broker_port) == NULL ||
        App_TasDtu_QueryContains("AT+CLIENTID?", client_id, "client_id") == 0U ||
        App_TasDtu_QueryContains("AT+MQTTPUB?", topic_up, "publish_topic") == 0U ||
        App_TasDtu_QueryContains("AT+MQTTSUB?", topic_down, "subscribe_topic") == 0U)
    {
        return 0U;
    }

    DTU_CONNECT_LOG_A("DTU", "[DTU] saved MQTT config verified broker=%s:%u pub=%s sub=%s",
                      TAS_DTU_BROKER_HOST,
                      (unsigned int)TAS_DTU_BROKER_PORT,
                      topic_up,
                      topic_down);
    return 1U;
}

static void App_TasDtu_UpdateMqttStatusLine(const char *line)
{
    if (line == NULL)
    {
        return;
    }

    if (App_TasDtu_ContainsIgnoreCase(line, "MQTT CONNECTED"))
    {
        g_tas_dtu_status.mqtt_configured = 1U;
        g_tas_dtu_status.transparent_ready = 1U;
        g_tas_dtu_status.state = TAS_DTU_STATE_TRANSPARENT;
        DTU_CONNECT_LOG_A("DTU", "[DTU] MQTT connected");
        return;
    }

    if (App_TasDtu_ContainsIgnoreCase(line, "MQTT CLOSED") ||
        App_TasDtu_ContainsIgnoreCase(line, "MQTT SUB LOST"))
    {
        g_tas_dtu_status.transparent_ready = 0U;
        g_tas_dtu_status.state = TAS_DTU_STATE_CONFIGURED;
        DTU_CONNECT_LOG_W("DTU", "[DTU] MQTT not ready: %.96s", line);
    }
}

static tas_dtu_result_t App_TasDtu_WaitMqttConnected(uint32_t timeout_ms)
{
    tas_dtu_result_t result;
    uint32_t start;
    uint32_t elapsed;
    uint32_t remaining;

    start = HAL_GetTick();
    do
    {
        elapsed = HAL_GetTick() - start;
        remaining = (elapsed < timeout_ms) ? (timeout_ms - elapsed) : 0U;
        if (remaining == 0U)
        {
            return TAS_DTU_RESULT_TIMEOUT;
        }

        result = App_TasDtu_WaitFor("MQTT CONNECTED",
                                    remaining,
                                    g_tas_dtu_at_resp,
                                    (uint16_t)sizeof(g_tas_dtu_at_resp));
        App_TasDtu_LogAtRx(result, "MQTT CONNECTED", g_tas_dtu_at_resp);

        if (result == TAS_DTU_RESULT_AT_ERROR)
        {
            elapsed = HAL_GetTick() - start;
            remaining = (elapsed < timeout_ms) ? (timeout_ms - elapsed) : 0U;
            DTU_CONNECT_LOG_W("DTU",
                   "[DTU] transient ERROR while waiting MQTT, continue (%lums remaining)",
                   (unsigned long)remaining);
        }
    } while (result == TAS_DTU_RESULT_AT_ERROR);

    if (result == TAS_DTU_RESULT_OK)
    {
        App_TasDtu_UpdateMqttStatusLine(g_tas_dtu_at_resp);
    }

    return result;
}

static uint8_t App_TasDtu_AskConnectIsChannel1Ready(void)
{
    const char *p;
    const char *last;

    p = strstr(g_tas_dtu_at_resp, "+ASKCONNECT:");
    last = p;

    while (p != NULL)
    {
        last = p;
        p = strstr(p + 12, "+ASKCONNECT:");
    }

    if (last == NULL)
    {
        return 0U;
    }

    p = strchr(last, ':');
    if (p == NULL)
    {
        return 0U;
    }

    p++;
    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }

    return (*p == '1') ? 1U : 0U;
}

static tas_dtu_result_t App_TasDtu_CollectAskConnect(uint32_t timeout_ms,
                                                     char *resp,
                                                     uint16_t resp_size)
{
    uint32_t start;
    uint16_t len;
    uint16_t total_rx;
    uint16_t yield_cnt;

    if ((resp == NULL) || (resp_size < 2U))
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    resp[0] = '\0';
    len = 0U;
    total_rx = 0U;
    yield_cnt = 0U;
    start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint8_t ch;

        /* 同 App_TasDtu_WaitFor：限制内层循环连续处理字节数，主动让出 CPU */
        while (BSP_UART_ReadByte(&g_tas_dtu_uart, &ch))
        {
            total_rx++;

            if (len < (uint16_t)(resp_size - 1U))
            {
                resp[len++] = (char)ch;
                resp[len] = '\0';
            }
            else
            {
                memmove(resp, &resp[1], (uint16_t)(resp_size - 2U));
                resp[resp_size - 2U] = (char)ch;
                resp[resp_size - 1U] = '\0';
                len = (uint16_t)(resp_size - 1U);
            }

            g_tas_dtu_status.last_rx_tick = HAL_GetTick();

            if (App_TasDtu_ResponseHasError(resp))
            {
                g_tas_dtu_status.at_error_count++;
                g_tas_dtu_status.last_rx_bytes = total_rx;
                return TAS_DTU_RESULT_AT_ERROR;
            }

            if (++yield_cnt >= 32U)
            {
                yield_cnt = 0U;
                taskYIELD();
            }
        }

        App_TasDtu_DelayMs(1U);
    }

    g_tas_dtu_status.last_rx_bytes = total_rx;
    if (total_rx > 0U)
    {
        return TAS_DTU_RESULT_OK;
    }

    g_tas_dtu_status.timeout_count++;
    return TAS_DTU_RESULT_TIMEOUT;
}

static tas_dtu_result_t App_TasDtu_QueryAskConnect(void)
{
    tas_dtu_result_t result;

    App_TasDtu_FlushInput();
    App_TasDtu_LogAtTx("AT+ASKCONNECT?");
    DTU_CONNECT_LOG_A("DTU", "[DTU] ASKCONNECT collect response window");

    result = App_TasDtu_SendRaw("AT+ASKCONNECT?\r\n");
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_LogAtRx(result, "+ASKCONNECT:", "");
        return result;
    }

    (void)App_TasDtu_WaitTxIdle(2000U);

    /*
     * 不能只等 OK。实测模块可能先吐 OK，随后才吐 +ASKCONNECT，
     * 如果提前返回会把真实连接状态遗留到下一条 AT 响应里。
     */
    result = App_TasDtu_CollectAskConnect(1200U,
                                          g_tas_dtu_at_resp,
                                          (uint16_t)sizeof(g_tas_dtu_at_resp));
    App_TasDtu_LogAtRx(result, "+ASKCONNECT:", g_tas_dtu_at_resp);

    return result;
}

static tas_dtu_result_t App_TasDtu_ExitCommandMode(void)
{
    tas_dtu_result_t result;

    result = App_TasDtu_SendCommand("ATO", "OK", 5000U);
    if (result == TAS_DTU_RESULT_OK)
    {
        g_tas_dtu_status.state = TAS_DTU_STATE_CONFIGURED;
        App_TasDtu_FlushInput();
    }
    else
    {
        DTU_CONNECT_LOG_W("DTU", "[DTU] ATO failed: %s", App_TasDtu_ResultName(result));
    }

    return result;
}

/* 只在 MQTT 已连通后的启动/重连阶段采样一次，禁止业务运行期周期查询 CSQ。 */
static void App_TasDtu_SampleCsqOnce(void)
{
    tas_dtu_result_t result;
    tas_dtu_result_t exit_result;
    int rssi = -1;
    const char *csq;

    if (g_tas_dtu_status.csq_rssi >= 0)
    {
        return;
    }

    result = App_TasDtu_EnterCommandMode();
    if (result != TAS_DTU_RESULT_OK)
    {
        /* 采样失败不能破坏已建立的 MQTT 透传状态。 */
        g_tas_dtu_status.mqtt_configured = 1U;
        g_tas_dtu_status.transparent_ready = 1U;
        g_tas_dtu_status.state = TAS_DTU_STATE_TRANSPARENT;
        return;
    }

    result = App_TasDtu_SendCommand("AT+CSQ", "OK", 3000U);
    if (result == TAS_DTU_RESULT_OK)
    {
        csq = strstr(g_tas_dtu_at_resp, "+CSQ:");
        if ((csq != NULL) && (sscanf(csq, "+CSQ: %d", &rssi) == 1) &&
            (rssi >= 0) && (rssi <= 31))
        {
            g_tas_dtu_status.csq_rssi = (int16_t)rssi;
            DTU_CONNECT_LOG_A("DTU", "[DTU] one-shot CSQ=%d", rssi);
        }
    }

    exit_result = App_TasDtu_ExitCommandMode();
    if (exit_result == TAS_DTU_RESULT_OK)
    {
        g_tas_dtu_status.mqtt_configured = 1U;
        g_tas_dtu_status.transparent_ready = 1U;
        g_tas_dtu_status.state = TAS_DTU_STATE_TRANSPARENT;
    }
}

static tas_dtu_result_t App_TasDtu_QuerySavedMqttLink(void)
{
    tas_dtu_result_t result;

    result = App_TasDtu_EnterCommandMode();
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_QueryAskConnect();
    if (result != TAS_DTU_RESULT_OK)
    {
        (void)App_TasDtu_ExitCommandMode();
        return result;
    }

    if ((App_TasDtu_ContainsIgnoreCase(g_tas_dtu_at_resp, "MQTT CONNECTED") == 0U) &&
        (App_TasDtu_AskConnectIsChannel1Ready() == 0U))
    {
        DTU_CONNECT_LOG_W("DTU", "[DTU] saved MQTT link not connected: %.96s", g_tas_dtu_at_resp);
        App_TasDtu_FlushInput();
        (void)App_TasDtu_ExitCommandMode();
        return TAS_DTU_RESULT_NOT_READY;
    }

    if (App_TasDtu_SavedMqttConfigMatches() == 0U)
    {
        g_tas_dtu_saved_config_mismatch = 1U;
        App_TasDtu_FlushInput();
        (void)App_TasDtu_ExitCommandMode();
        return TAS_DTU_RESULT_NOT_READY;
    }

    App_TasDtu_FlushInput();
    result = App_TasDtu_ExitCommandMode();
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    g_tas_dtu_status.mqtt_configured = 1U;
    g_tas_dtu_status.transparent_ready = 1U;
    g_tas_dtu_status.state = TAS_DTU_STATE_TRANSPARENT;
    DTU_CONNECT_LOG_A("DTU", "[DTU] saved MQTT link and config are valid");
    return TAS_DTU_RESULT_OK;
}

static tas_dtu_result_t App_TasDtu_QuerySavedMqttLinkAtBaud(uint32_t baud)
{
    tas_dtu_result_t result;

    result = App_TasDtu_SetUartBaud(baud);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    DTU_CONNECT_LOG_A("DTU", "[DTU] probe saved MQTT link at %lu", (unsigned long)baud);
    return App_TasDtu_QuerySavedMqttLink();
}

static tas_dtu_result_t App_TasDtu_PollMqttLinkAtBaud(uint32_t baud,
                                                        uint32_t poll_interval_ms,
                                                        uint32_t max_polls)
{
    tas_dtu_result_t result;
    uint32_t i;

    for (i = 0U; i < max_polls; i++)
    {
        DTU_CONNECT_LOG_A("DTU", "[DTU] poll ASKCONNECT at %lu (%lu/%lu)",
               (unsigned long)baud,
               (unsigned long)(i + 1U),
               (unsigned long)max_polls);

        result = App_TasDtu_QuerySavedMqttLinkAtBaud(baud);
        if (g_tas_dtu_saved_config_mismatch != 0U)
        {
            return TAS_DTU_RESULT_NOT_READY;
        }
        if (result == TAS_DTU_RESULT_OK)
        {
            return TAS_DTU_RESULT_OK;
        }

        if (result != TAS_DTU_RESULT_NOT_READY)
        {
            DTU_CONNECT_LOG_W("DTU", "[DTU] poll AT error at %lu: %s",
                   (unsigned long)baud, App_TasDtu_ResultName(result));
            return result;
        }

        if (i < (max_polls - 1U))
        {
            App_TasDtu_DelayMs(poll_interval_ms);
        }
    }

    return TAS_DTU_RESULT_NOT_READY;
}

static tas_dtu_result_t App_TasDtu_BuildTelemetryJson(char *buf,
                                                      uint16_t size,
                                                      const char *type)
{
    if (LiftIot_BuildTelemetryJson(buf,
                                       size,
                                       type,
                                       g_tas_dtu_status.tx_seq,
                                       App_TasDtu_StateName(g_tas_dtu_status.state),
                                       g_tas_dtu_status.csq_rssi) != LIFT_IOT_OK)
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    return TAS_DTU_RESULT_OK;
}

static const char *App_TasDtu_V1CommandResult(const char *result_text)
{
    if (result_text == NULL)
    {
        return "failed";
    }

    if ((strstr(result_text, "denied") != NULL) ||
        (strstr(result_text, "unknown") != NULL))
    {
        return "rejected";
    }

    if ((strstr(result_text, "failed") != NULL) ||
        (strstr(result_text, "error") != NULL))
    {
        return "failed";
    }

    return "succeeded";
}

static void App_TasDtu_CopyCacheString(char *dst, uint16_t dst_size, const char *src)
{
    if ((dst == NULL) || (dst_size == 0U))
    {
        return;
    }

    if (src == NULL)
    {
        src = "";
    }

    (void)snprintf(dst, dst_size, "%s", src);
}

static int16_t App_TasDtu_FindCommandCacheIndex(const char *msg_id)
{
    uint8_t i;

    if ((msg_id == NULL) || (msg_id[0] == '\0'))
    {
        return -1;
    }

    for (i = 0U; i < TAS_DTU_MSG_ID_CACHE_SIZE; i++)
    {
        if (strcmp(g_tas_dtu_cmd_cache[i].msg_id, msg_id) == 0)
        {
            return (int16_t)i;
        }
    }

    return -1;
}

static uint8_t App_TasDtu_LoadCachedCommandResult(const char *msg_id,
                                                  char *cmd,
                                                  uint16_t cmd_size,
                                                  char *result_text,
                                                  uint16_t result_size,
                                                  char *event,
                                                  uint16_t event_size)
{
    int16_t index = App_TasDtu_FindCommandCacheIndex(msg_id);

    if (index < 0)
    {
        return 0U;
    }

    App_TasDtu_CopyCacheString(cmd, cmd_size, g_tas_dtu_cmd_cache[index].cmd);
    App_TasDtu_CopyCacheString(result_text, result_size, g_tas_dtu_cmd_cache[index].result_text);
    App_TasDtu_CopyCacheString(event, event_size, g_tas_dtu_cmd_cache[index].event);
    return 1U;
}

static void App_TasDtu_SaveCommandResultCache(const char *cmd,
                                              const char *msg_id,
                                              const char *result_text,
                                              const char *event)
{
    int16_t index;
    uint8_t slot;

    if ((msg_id == NULL) || (msg_id[0] == '\0'))
    {
        return;
    }

    index = App_TasDtu_FindCommandCacheIndex(msg_id);
    if (index >= 0)
    {
        slot = (uint8_t)index;
    }
    else
    {
        slot = g_tas_dtu_cmd_cache_next;
        g_tas_dtu_cmd_cache_next++;
        if (g_tas_dtu_cmd_cache_next >= TAS_DTU_MSG_ID_CACHE_SIZE)
        {
            g_tas_dtu_cmd_cache_next = 0U;
        }
    }

    App_TasDtu_CopyCacheString(g_tas_dtu_cmd_cache[slot].msg_id,
                               (uint16_t)sizeof(g_tas_dtu_cmd_cache[slot].msg_id),
                               msg_id);
    App_TasDtu_CopyCacheString(g_tas_dtu_cmd_cache[slot].cmd,
                               (uint16_t)sizeof(g_tas_dtu_cmd_cache[slot].cmd),
                               cmd);
    App_TasDtu_CopyCacheString(g_tas_dtu_cmd_cache[slot].result_text,
                               (uint16_t)sizeof(g_tas_dtu_cmd_cache[slot].result_text),
                               result_text);
    App_TasDtu_CopyCacheString(g_tas_dtu_cmd_cache[slot].event,
                               (uint16_t)sizeof(g_tas_dtu_cmd_cache[slot].event),
                               event);
}

static tas_dtu_result_t App_TasDtu_ReportCommandResult(const char *cmd,
                                                       const char *msg_id,
                                                       const char *result_text,
                                                       const char *event)
{
    char json[TAS_DTU_JSON_SIZE];
    int len;
    const char *v1_result;
    const char *reason;
    app_maintenance_status_t maintenance;
    tas_dtu_result_t send_result;

    if (cmd == NULL)
    {
        cmd = "unknown";
    }

    if (msg_id == NULL)
    {
        msg_id = "";
    }

    if (result_text == NULL)
    {
        result_text = "ok";
    }

    if (event == NULL)
    {
        event = result_text;
    }

    v1_result = App_TasDtu_V1CommandResult(result_text);
    if ((strcmp(result_text, "maintenance_done") == 0) ||
        (strcmp(result_text, "usage_reset") == 0))
    {
        v1_result = result_text;
    }
    reason = (strcmp(v1_result, "succeeded") == 0) ? "" : result_text;
    App_Maintenance_GetStatus(&maintenance);

    len = snprintf(json, sizeof(json),
                   "{\"v\":1,\"type\":\"command_response\",\"chip_uid\":\"%s\",\"uid\":\"%s\","
                   "\"product_type\":\"%s\",\"seq\":%lu,\"tick_ms\":%lu,"
                   "\"cmd\":\"%s\",\"msg_id\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\","
                   "\"event\":\"%s\",\"maintenance\":{\"total_lift_count\":%lu,"
                   "\"maintenance_lift_count\":%lu,\"maintenance_threshold\":5000,"
                   "\"maintenance_count\":%lu,\"last_maintenance_total\":%lu,"
                   "\"maintenance_due\":%u,\"usage_epoch\":%lu},"
                   "\"maintenance_due\":%u,\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   App_TasDtu_ChipUidString(),
                   App_TasDtu_ChipUidString(),
                   App_Product_TypeName(g_product_type),
                   (unsigned long)g_tas_dtu_status.tx_seq,
                   (unsigned long)HAL_GetTick(),
                   cmd,
                   msg_id,
                   v1_result,
                   reason,
                   event,
                   (unsigned long)maintenance.total_lift_count,
                   (unsigned long)maintenance.maintenance_lift_count,
                   (unsigned long)maintenance.maintenance_count,
                   (unsigned long)maintenance.last_maintenance_total,
                   (unsigned int)maintenance.maintenance_due,
                   (unsigned long)maintenance.usage_epoch,
                   (unsigned int)maintenance.maintenance_due,
                   App_TasDtu_StateName(g_tas_dtu_status.state),
                   (int)g_tas_dtu_status.csq_rssi);
    if ((len < 0) || (len >= (int)sizeof(json)))
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    App_TasDtu_SaveCommandResultCache(cmd, msg_id, result_text, event);
    send_result = App_TasDtu_SendJson(json);
    if (send_result == TAS_DTU_RESULT_OK)
    {
        DTU_TRANSFER_LOG_A("DTU", "[DTU] CMD RESP TX cmd=%s msg_id=%s result=%s",
                           cmd, msg_id, v1_result);
    }
    else
    {
        DTU_TRANSFER_LOG_E("DTU", "[DTU] CMD RESP TX failed cmd=%s msg_id=%s result=%s error=%s",
                           cmd, msg_id, v1_result, App_TasDtu_ResultName(send_result));
    }
    return send_result;
}

static uint8_t App_TasDtu_JsonGetString(const char *json,
                                        const char *key,
                                        char *out,
                                        uint16_t out_size)
{
    char pattern[32];
    const char *p;
    const char *start;
    const char *end;
    uint16_t len;

    if ((json == NULL) || (key == NULL) || (out == NULL) || (out_size == 0U))
    {
        return 0U;
    }

    if (snprintf(pattern, sizeof(pattern), "\"%s\":\"", key) >= (int)sizeof(pattern))
    {
        return 0U;
    }

    p = strstr(json, pattern);
    if (p == NULL)
    {
        return 0U;
    }

    start = p + strlen(pattern);
    end = strchr(start, '"');
    if (end == NULL)
    {
        return 0U;
    }

    len = (uint16_t)(end - start);
    if (len >= out_size)
    {
        len = (uint16_t)(out_size - 1U);
    }

    memcpy(out, start, len);
    out[len] = '\0';
    return 1U;
}

static uint8_t App_TasDtu_JsonGetUint(const char *json,
                                      const char *key,
                                      uint32_t *out)
{
    char pattern[32];
    const char *p;
    unsigned long value;

    if ((json == NULL) || (key == NULL) || (out == NULL))
    {
        return 0U;
    }

    if (snprintf(pattern, sizeof(pattern), "\"%s\":", key) >= (int)sizeof(pattern))
    {
        return 0U;
    }

    p = strstr(json, pattern);
    if (p == NULL)
    {
        return 0U;
    }

    p += strlen(pattern);
    if (sscanf(p, "%lu", &value) != 1)
    {
        return 0U;
    }

    *out = (uint32_t)value;
    return 1U;
}

static uint8_t App_TasDtu_JsonHasCommand(const char *json, const char *cmd)
{
    char pattern[64];

    if ((json == NULL) || (cmd == NULL))
    {
        return 0U;
    }

    if (snprintf(pattern, sizeof(pattern), "\"cmd\":\"%s\"", cmd) >= (int)sizeof(pattern))
    {
        return 0U;
    }

    if (strstr(json, pattern) != NULL)
    {
        return 1U;
    }

    if (snprintf(pattern, sizeof(pattern), "\"command\":\"%s\"", cmd) >= (int)sizeof(pattern))
    {
        return 0U;
    }

    return strstr(json, pattern) != NULL;
}

static uint8_t App_TasDtu_SetConfigU16(const char *json,
                                       const char *key,
                                       uint16_t *field,
                                       uint32_t min_value,
                                       uint32_t max_value)
{
    uint32_t value;

    if ((field == NULL) || (App_TasDtu_JsonGetUint(json, key, &value) == 0U))
    {
        return 0U;
    }

    if (value < min_value)
    {
        value = min_value;
    }
    if (value > max_value)
    {
        value = max_value;
    }

    *field = (uint16_t)value;
    return 1U;
}

static uint8_t App_TasDtu_SetConfigU8(const char *json,
                                      const char *key,
                                      uint8_t *field,
                                      uint32_t min_value,
                                      uint32_t max_value)
{
    uint32_t value;

    if ((field == NULL) || (App_TasDtu_JsonGetUint(json, key, &value) == 0U))
    {
        return 0U;
    }

    if (value < min_value)
    {
        value = min_value;
    }
    if (value > max_value)
    {
        value = max_value;
    }

    *field = (uint8_t)value;
    return 1U;
}

static lift_iot_result_t App_TasDtu_ApplyConfigCommand(const char *json)
{
    uint8_t changed = 0U;

    changed |= App_TasDtu_SetConfigU16(json, "motor_to_valve_delay_ms",
                                       &g_config.motor_to_valve_delay_ms, 0U, 5000U);
    changed |= App_TasDtu_SetConfigU16(json, "motor_hold_ms",
                                       &g_config.motor_hold_ms, 0U, 10000U);
    changed |= App_TasDtu_SetConfigU16(json, "sub_motor_hold_ms",
                                       &g_config.sub_motor_hold_ms, 0U, 10000U);
    changed |= App_TasDtu_SetConfigU8(json, "module_enable_mask",
                                      &g_config.module_enable_mask, 0U, 0xFFU);
    changed |= App_TasDtu_SetConfigU16(json, "photoelectric_debounce_ms",
                                       &g_config.photoelectric_debounce_ms, 0U, 1000U);
    changed |= App_TasDtu_SetConfigU16(json, "estop_debounce_ms",
                                       &g_config.estop_debounce_ms, 0U, 1000U);
    changed |= App_TasDtu_SetConfigU16(json, "tolerance_up",
                                       &g_config.tolerance_up, 0U, 10000U);
    changed |= App_TasDtu_SetConfigU16(json, "tolerance_down",
                                       &g_config.tolerance_down, 0U, 10000U);
    changed |= App_TasDtu_SetConfigU16(json, "stall_timeout_ms",
                                       &g_config.stall_timeout_ms, 100U, 30000U);
    changed |= App_TasDtu_SetConfigU16(json, "balance_wait_max_ms",
                                       &g_config.balance_wait_max_ms, 100U, 30000U);
    changed |= App_TasDtu_SetConfigU16(json, "collision_debounce_ms",
                                       &g_config.collision_debounce_ms, 0U, 1000U);
    changed |= App_TasDtu_SetConfigU16(json, "secondary_descent_pulses",
                                       &g_config.secondary_descent_pulses, 0U, 10000U);
    changed |= App_TasDtu_SetConfigU8(json, "dual_column_mode",
                                      &g_config.dual_column_mode, 0U, 1U);
    changed |= App_TasDtu_SetConfigU8(json, "screw_lead_mm",
                                      &g_config.screw_lead_mm, 1U, 100U);
    changed |= App_TasDtu_SetConfigU16(json, "max_pulses",
                                       &g_config.max_pulses, 0U, 60000U);

    if ((changed != 0U) && (App_W25Qxx_Config_Save() != W25Q_OK))
    {
        return LIFT_IOT_DENIED;
    }

    return LIFT_IOT_OK;
}

static tas_dtu_result_t App_TasDtu_ReportConfigResult(const char *cmd,
                                                      const char *msg_id,
                                                      const char *result_text,
                                                      const char *event)
{
    char json[TAS_DTU_JSON_SIZE];
    int len;
    uint32_t now = HAL_GetTick();

    len = snprintf(json, sizeof(json),
                   "{\"type\":\"status\",\"device\":\"%s\",\"seq\":%lu,\"tick\":%lu,"
                   "\"uptime_ms\":%lu,\"event\":\"%s\",\"cmd\":\"%s\",\"msg_id\":\"%s\","
                   "\"result\":\"%s\",\"product_type\":\"%s\","
                   "\"config\":{\"motor_to_valve_delay_ms\":%u,\"motor_hold_ms\":%u,"
                   "\"sub_motor_hold_ms\":%u,\"module_enable_mask\":%u,"
                   "\"photoelectric_debounce_ms\":%u,\"estop_debounce_ms\":%u,"
                   "\"tolerance_up\":%u,\"tolerance_down\":%u,\"stall_timeout_ms\":%u,"
                   "\"balance_wait_max_ms\":%u,\"collision_debounce_ms\":%u,"
                   "\"secondary_descent_pulses\":%u,\"dual_column_mode\":%u,"
                   "\"screw_lead_mm\":%u,\"max_pulses\":%u},"
                   "\"dtu\":{\"state\":\"%s\",\"csq\":%d}}",
                   LIFT_IOT_DEVICE_ID,
                   (unsigned long)g_tas_dtu_status.tx_seq,
                   (unsigned long)now,
                   (unsigned long)now,
                   event,
                   cmd,
                   (msg_id != NULL) ? msg_id : "",
                   result_text,
                   App_Product_TypeName(g_product_type),
                   (unsigned int)g_config.motor_to_valve_delay_ms,
                   (unsigned int)g_config.motor_hold_ms,
                   (unsigned int)g_config.sub_motor_hold_ms,
                   (unsigned int)g_config.module_enable_mask,
                   (unsigned int)g_config.photoelectric_debounce_ms,
                   (unsigned int)g_config.estop_debounce_ms,
                   (unsigned int)g_config.tolerance_up,
                   (unsigned int)g_config.tolerance_down,
                   (unsigned int)g_config.stall_timeout_ms,
                   (unsigned int)g_config.balance_wait_max_ms,
                   (unsigned int)g_config.collision_debounce_ms,
                   (unsigned int)g_config.secondary_descent_pulses,
                   (unsigned int)g_config.dual_column_mode,
                   (unsigned int)g_config.screw_lead_mm,
                   (unsigned int)g_config.max_pulses,
                   App_TasDtu_StateName(g_tas_dtu_status.state),
                   (int)g_tas_dtu_status.csq_rssi);

    if ((len < 0) || (len >= (int)sizeof(json)))
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    return App_TasDtu_SendJson(json);
}

static uint8_t App_TasDtu_IsAcceptedCommandJson(const char *json)
{
    char type[16];
    char device[48];
    char device_id[48];
    char uid[64];

    if (json == NULL)
    {
        return 0U;
    }

    type[0] = '\0';
    device[0] = '\0';
    device_id[0] = '\0';
    uid[0] = '\0';
    (void)App_TasDtu_JsonGetString(json, "type", type, sizeof(type));
    (void)App_TasDtu_JsonGetString(json, "device", device, sizeof(device));
    (void)App_TasDtu_JsonGetString(json, "device_id", device_id, sizeof(device_id));
    (void)App_TasDtu_JsonGetString(json, "uid", uid, sizeof(uid));
    if (uid[0] == '\0')
    {
        (void)App_TasDtu_JsonGetString(json, "chip_uid", uid, sizeof(uid));
    }
    if (uid[0] == '\0')
    {
        (void)App_TasDtu_JsonGetString(json, "mcu_uid", uid, sizeof(uid));
    }
    if (uid[0] == '\0')
    {
        (void)App_TasDtu_JsonGetString(json, "target_uid", uid, sizeof(uid));
    }

    /*
     * 只处理平台下发命令，忽略本机 telemetry/status 回环。
     */
    if (strcmp(type, "command") != 0)
    {
        return 0U;
    }

    if (uid[0] != '\0')
    {
        return App_TasDtu_UidMatchesLocal(uid);
    }

    if ((device[0] == '\0') && (device_id[0] != '\0'))
    {
        (void)snprintf(device, sizeof(device), "%s", device_id);
    }

    if ((device[0] == '\0') || (strcmp(device, LIFT_IOT_DEVICE_ID) != 0))
    {
        return 0U;
    }

    return 1U;
}

static void App_TasDtu_HandleJsonCommand(const char *json)
{
    char password[32];
    char account[32];
    char msg_id[48];
    char cmd_name[32];
    char cached_cmd[32];
    char cached_result[32];
    char cached_event[32];
    char column[16];
    char direction[16];
    uint32_t duration_ms;
    lift_iot_result_t lift_result;

    if (json == NULL)
    {
        return;
    }

    if (App_TasDtu_IsAcceptedCommandJson(json) == 0U)
    {
        return;
    }

    g_tas_dtu_status.rx_json_count++;
    account[0] = '\0';
    msg_id[0] = '\0';
    cmd_name[0] = '\0';
    (void)App_TasDtu_JsonGetString(json, "account", account, sizeof(account));
    (void)App_TasDtu_JsonGetString(json, "msg_id", msg_id, sizeof(msg_id));
    (void)App_TasDtu_JsonGetString(json, "cmd", cmd_name, sizeof(cmd_name));
    if (cmd_name[0] == '\0')
    {
        (void)App_TasDtu_JsonGetString(json, "command", cmd_name, sizeof(cmd_name));
    }
    if (account[0] == '\0')
    {
        (void)snprintf(account, sizeof(account), "%s", "web");
    }

    DTU_TRANSFER_LOG_A("DTU", "[DTU] CMD RX cmd=%s msg_id=%s",
                       (cmd_name[0] != '\0') ? cmd_name : "unknown",
                       (msg_id[0] != '\0') ? msg_id : "(empty)");

    if (App_TasDtu_LoadCachedCommandResult(msg_id,
                                           cached_cmd,
                                           (uint16_t)sizeof(cached_cmd),
                                           cached_result,
                                           (uint16_t)sizeof(cached_result),
                                           cached_event,
                                           (uint16_t)sizeof(cached_event)) != 0U)
    {
        (void)App_TasDtu_ReportCommandResult(cached_cmd, msg_id, cached_result, cached_event);
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "ping"))
    {
        (void)App_TasDtu_ReportCommandResult("ping", msg_id, "pong", "pong");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "get_status") ||
        App_TasDtu_JsonHasCommand(json, "report_now"))
    {
        tas_dtu_result_t telemetry_result = App_TasDtu_ReportTelemetry();
        if (telemetry_result != TAS_DTU_RESULT_OK)
        {
            DTU_TRANSFER_LOG_E("DTU", "[DTU] get_status telemetry failed msg_id=%s error=%s",
                               msg_id, App_TasDtu_ResultName(telemetry_result));
            (void)App_TasDtu_ReportCommandResult("get_status", msg_id,
                                                 "telemetry_failed", "report_failed");
            return;
        }
        (void)App_TasDtu_ReportCommandResult("get_status", msg_id, "reported", "report_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "lock"))
    {
        (void)LiftIot_SetLocked(1U, account);
        (void)App_TasDtu_ReportCommandResult("lock", msg_id, "locked", "lock_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "unlock"))
    {
        (void)LiftIot_SetLocked(0U, account);
        (void)App_TasDtu_ReportCommandResult("unlock", msg_id, "unlocked", "unlock_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "buzzer_on"))
    {
        App_Buzzer_Alarm(0U);
        (void)App_TasDtu_ReportCommandResult("buzzer_on", msg_id, "buzzer_on", "buzzer_on_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "buzzer_off"))
    {
        App_Buzzer_Off();
        (void)App_TasDtu_ReportCommandResult("buzzer_off", msg_id, "buzzer_off", "buzzer_off_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "admin_enter"))
    {
        password[0] = '\0';
        (void)App_TasDtu_JsonGetString(json, "password", password, sizeof(password));
        lift_result = LiftIot_EnterAdmin(password, account);
        (void)App_TasDtu_ReportCommandResult("admin_enter",
                                             msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "admin_entered" : "admin_denied",
                                             (lift_result == LIFT_IOT_OK) ? "admin_enter_ok" : "admin_enter_denied");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "admin_exit"))
    {
        LiftIot_ExitAdmin();
        (void)App_TasDtu_ReportCommandResult("admin_exit", msg_id, "admin_exited", "admin_exit_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "fault_clear"))
    {
        lift_result = LiftIot_ClearFault(account);
        (void)App_TasDtu_ReportCommandResult("fault_clear",
                                             msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "fault_cleared" : "fault_clear_denied",
                                             (lift_result == LIFT_IOT_OK) ? "fault_clear_ok" : "fault_clear_denied");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "clear_alarm"))
    {
        lift_result = LiftIot_HandleClearAlarm(account);
        (void)App_TasDtu_ReportCommandResult("clear_alarm",
                                             msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "alarm_cleared" : "alarm_clear_denied",
                                             (lift_result == LIFT_IOT_OK) ? "clear_alarm_ok" : "clear_alarm_denied");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "get_config"))
    {
        (void)App_TasDtu_ReportConfigResult("get_config",
                                            msg_id,
                                            "config_reported",
                                            "config_report");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "set_config"))
    {
        lift_result = App_TasDtu_ApplyConfigCommand(json);
        (void)App_TasDtu_ReportConfigResult("set_config",
                                            msg_id,
                                            (lift_result == LIFT_IOT_OK) ? "config_set" : "config_set_denied",
                                            (lift_result == LIFT_IOT_OK) ? "set_config_ok" : "set_config_denied");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "admin_jog"))
    {
        uint8_t column_index = 0U;
        uint8_t direction_up = 1U;

        column[0] = '\0';
        direction[0] = '\0';
        (void)App_TasDtu_JsonGetString(json, "column", column, sizeof(column));
        (void)App_TasDtu_JsonGetString(json, "direction", direction, sizeof(direction));
        duration_ms = 300U;
        (void)App_TasDtu_JsonGetUint(json, "duration_ms", &duration_ms);

        if (strcmp(column, "right") == 0)
        {
            column_index = 1U;
        }

        if (strcmp(direction, "down") == 0)
        {
            direction_up = 0U;
        }

        lift_result = LiftIot_AdminJog(column_index, direction_up, duration_ms, account);
        (void)App_TasDtu_ReportCommandResult("admin_jog",
                                             msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "admin_jog_ok" : "admin_jog_denied",
                                             (lift_result == LIFT_IOT_OK) ? "admin_jog_ok" : "admin_jog_denied");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "maintenance_done"))
    {
        lift_result = LiftIot_MaintenanceDone(account, msg_id);
        (void)App_TasDtu_ReportCommandResult("maintenance_done", msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "maintenance_done" : "maintenance_failed",
                                             (lift_result == LIFT_IOT_OK) ? "maintenance_done_ok" : "maintenance_done_failed");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "reset_usage"))
    {
        lift_result = LiftIot_ResetUsage(account, msg_id);
        (void)App_TasDtu_ReportCommandResult("reset_usage", msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "usage_reset" : "usage_reset_denied",
                                             (lift_result == LIFT_IOT_OK) ? "usage_reset_ok" : "usage_reset_denied");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "config_mqtt"))
    {
        (void)App_TasDtu_ReportStatus("config_mqtt_start");
        if (App_TasDtu_ConfigureMqtt() == TAS_DTU_RESULT_OK)
        {
            (void)App_TasDtu_ReportCommandResult("config_mqtt", msg_id, "configured", "config_mqtt_ok");
        }
        else
        {
            (void)App_TasDtu_ReportCommandResult("config_mqtt", msg_id, "config_failed", "config_mqtt_failed");
        }
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "reboot_dtu"))
    {
        (void)App_TasDtu_ReportCommandResult("reboot_dtu", msg_id, "rebooting", "reboot_dtu");
        (void)App_TasDtu_Reboot();
        return;
    }

    (void)App_TasDtu_ReportCommandResult((cmd_name[0] != '\0') ? cmd_name : "unknown",
                                         msg_id,
                                         "unknown_command",
                                         "unknown_command");
}

static void App_TasDtu_HandleLine(const char *line)
{
    if ((line == NULL) || (line[0] == '\0'))
    {
        return;
    }

    App_TasDtu_UpdateMqttStatusLine(line);

    if (strchr(line, '{') != NULL)
    {
        App_TasDtu_HandleJsonCommand(line);
    }
}

/* ================= Public API ================= */

tas_dtu_result_t App_TasDtu_Init(void)
{
    if (BSP_UART_Init(&g_tas_dtu_uart,
                      &huart3,
                      g_tas_dtu_rx_dma_buf,
                      TAS_DTU_RX_DMA_SIZE) != BSP_UART_OK)
    {
        App_TasDtu_SetError(TAS_DTU_RESULT_UART_ERROR);
        return TAS_DTU_RESULT_UART_ERROR;
    }

    BSP_UART_SetDirection(&g_tas_dtu_uart, RS485_Uart3_Dir_GPIO_Port, RS485_Uart3_Dir_Pin);

    memset(&g_tas_dtu_status, 0, sizeof(g_tas_dtu_status));
    g_tas_dtu_status.state = TAS_DTU_STATE_UART_READY;
    g_tas_dtu_status.csq_rssi = -1;
    g_tas_dtu_status.current_baud = huart3.Init.BaudRate;
    LiftIot_Init();
    App_TasDtu_FlushInput();

    return TAS_DTU_RESULT_OK;
}

tas_dtu_result_t App_TasDtu_ConfigureMqtt(void)
{
    tas_dtu_result_t result;
    char topic_up[96];
    char topic_down[96];

    g_tas_dtu_status.mqtt_configured = 0U;
    g_tas_dtu_status.transparent_ready = 0U;
    g_tas_dtu_saved_config_mismatch = 0U;

    App_TasDtu_BuildV1Topic(topic_up, sizeof(topic_up), "up");
    App_TasDtu_BuildV1Topic(topic_down, sizeof(topic_down), "down");
    DTU_CONNECT_LOG_A("DTU",
           "[DTU] MQTT config start broker=%s:%u pub=%s sub=%s",
           TAS_DTU_BROKER_HOST,
           (unsigned int)TAS_DTU_BROKER_PORT,
           topic_up,
           topic_down);

    result = App_TasDtu_EnterCommandMode();
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        DTU_CONNECT_LOG_E("DTU", "[DTU] MQTT config failed at command mode: %s", App_TasDtu_ResultName(result));
        return result;
    }

    result = App_TasDtu_ConfigMqttChannel();
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        DTU_CONNECT_LOG_E("DTU", "[DTU] MQTT config failed: %s", App_TasDtu_ResultName(result));
        return result;
    }

    g_tas_dtu_status.mqtt_configured = 1U;
    g_tas_dtu_status.state = TAS_DTU_STATE_CONFIGURED;

    result = App_TasDtu_SendCommand("AT&W", "OK", 5000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        DTU_CONNECT_LOG_E("DTU", "[DTU] MQTT config save failed: %s", App_TasDtu_ResultName(result));
        return result;
    }

    DTU_CONNECT_LOG_A("DTU", "[DTU] MQTT config saved, reboot DTU");
    result = App_TasDtu_SendCommand("AT+CFUN=1,1", "OK", 3000U);
    if ((result != TAS_DTU_RESULT_OK) && (result != TAS_DTU_RESULT_TIMEOUT))
    {
        App_TasDtu_SetError(result);
        DTU_CONNECT_LOG_E("DTU", "[DTU] MQTT config reboot failed: %s", App_TasDtu_ResultName(result));
        return result;
    }

    g_tas_dtu_status.transparent_ready = 0U;
    g_tas_dtu_status.state = TAS_DTU_STATE_CONFIGURED;
    App_TasDtu_FlushInput();

    DTU_CONNECT_LOG_A("DTU", "[DTU] wait MQTT connected status");
    result = App_TasDtu_WaitMqttConnected(TAS_DTU_CONFIGURE_WAIT_MS);
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        DTU_CONNECT_LOG_E("DTU", "[DTU] MQTT connect status timeout: %s", App_TasDtu_ResultName(result));
        return result;
    }

    App_TasDtu_SampleCsqOnce();
    App_TasDtu_FlushInput();
    DTU_CONNECT_LOG_A("DTU", "[DTU] MQTT transparent mode ready");
    return TAS_DTU_RESULT_OK;
}

tas_dtu_result_t App_TasDtu_StartMqtt(void)
{
    tas_dtu_result_t result;

    g_tas_dtu_status.mqtt_configured = 0U;
    g_tas_dtu_status.transparent_ready = 0U;
    g_tas_dtu_saved_config_mismatch = 0U;

    (void)App_TasDtu_SetUartBaud(TAS_DTU_PRIMARY_BAUD);
    App_TasDtu_FlushInput();

    DTU_CONNECT_LOG_A("DTU", "[DTU] startup wait MQTT URC (%lus)",
           (unsigned long)TAS_DTU_STARTUP_WAIT_MS / 1000UL);
    result = App_TasDtu_WaitMqttConnected(TAS_DTU_STARTUP_WAIT_MS);
    if (result == TAS_DTU_RESULT_OK)
    {
        DTU_CONNECT_LOG_A("DTU", "[DTU] startup MQTT URC received, verify saved config");
        result = App_TasDtu_QuerySavedMqttLink();
        if (result == TAS_DTU_RESULT_OK)
        {
            App_TasDtu_SampleCsqOnce();
            App_TasDtu_FlushInput();
            DTU_CONNECT_LOG_A("DTU", "[DTU] MQTT transparent mode ready");
            return TAS_DTU_RESULT_OK;
        }
    }

    DTU_CONNECT_LOG_W("DTU", "[DTU] no URC, poll ASKCONNECT at %lu",
           (unsigned long)TAS_DTU_PRIMARY_BAUD);
    result = App_TasDtu_PollMqttLinkAtBaud(TAS_DTU_PRIMARY_BAUD,
                                            TAS_DTU_LINK_POLL_INTERVAL_MS,
                                            TAS_DTU_LINK_POLL_COUNT_9600);
    if (result == TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SampleCsqOnce();
        return TAS_DTU_RESULT_OK;
    }

    if ((result == TAS_DTU_RESULT_NOT_READY) &&
        (g_tas_dtu_saved_config_mismatch == 0U))
    {
        DTU_CONNECT_LOG_W("DTU", "[DTU] poll at %lu still not connected, try %lu",
               (unsigned long)TAS_DTU_PRIMARY_BAUD,
               (unsigned long)TAS_DTU_FALLBACK_BAUD);
        result = App_TasDtu_PollMqttLinkAtBaud(TAS_DTU_FALLBACK_BAUD,
                                                TAS_DTU_LINK_POLL_INTERVAL_MS,
                                                TAS_DTU_LINK_POLL_COUNT_115200);
        if (result == TAS_DTU_RESULT_OK)
        {
            App_TasDtu_SampleCsqOnce();
            return TAS_DTU_RESULT_OK;
        }
    }

    if (result != TAS_DTU_RESULT_NOT_READY)
    {
        App_TasDtu_SetError(result);
        DTU_CONNECT_LOG_E("DTU", "[DTU] poll AT error: %s", App_TasDtu_ResultName(result));
        return result;
    }

    DTU_CONNECT_LOG_W("DTU", "[DTU] all polls NOT_READY, force configure at %lu",
           (unsigned long)TAS_DTU_PRIMARY_BAUD);
    (void)App_TasDtu_SetUartBaud(TAS_DTU_PRIMARY_BAUD);
    result = App_TasDtu_ConfigureMqtt();
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
    }

    return result;
}

tas_dtu_result_t App_TasDtu_Reboot(void)
{
    tas_dtu_result_t result;

    DTU_CONNECT_LOG_A("DTU", "[DTU] reboot request");

    result = App_TasDtu_EnterCommandMode();
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        DTU_CONNECT_LOG_E("DTU", "[DTU] reboot failed at command mode: %s", App_TasDtu_ResultName(result));
        return result;
    }

    result = App_TasDtu_SendCommand("AT+CFUN=1,1", "OK", 3000U);
    App_TasDtu_DelayMs(5000U);
    g_tas_dtu_status.transparent_ready = 0U;
    g_tas_dtu_status.state = TAS_DTU_STATE_CONFIGURED;
    App_TasDtu_FlushInput();

    DTU_CONNECT_LOG_A("DTU", "[DTU] reboot command result=%s", App_TasDtu_ResultName(result));
    return result;
}

static void App_TasDtu_ProcessRiseQueue(void)
{
    w25q_rise_event_t event;
    uint32_t now = HAL_GetTick();

    if (App_TasDtu_IsTransparentReady() == 0U)
    {
        /* This function runs only from the DTU task, not the 1 ms lift task. */
        (void)App_W25Qxx_RiseQueue_PersistPending();
        return;
    }

    if (App_W25Qxx_RiseQueue_HasFlashPending() != 0U)
    {
        /* Keep new events behind older offline records. */
        (void)App_W25Qxx_RiseQueue_PersistPending();
    }

    if ((now - g_tas_dtu_rise_last_report_tick) < 200U)
    {
        return;
    }

    if (App_W25Qxx_RiseQueue_Peek(&event) == 0U)
    {
        return;
    }

    g_tas_dtu_rise_last_report_tick = now;
    if (App_TasDtu_ReportRiseCount(&event) == TAS_DTU_RESULT_OK)
    {
        (void)App_W25Qxx_RiseQueue_Ack(&event);
    }
    else
    {
        /* A false-ready or UART error must not lose the event. */
        (void)App_W25Qxx_RiseQueue_PersistPending();
    }
}

void App_TasDtu_ProcessRx(void)
{
    uint8_t ch;
    uint16_t processed = 0U;

    while ((processed < TAS_DTU_RX_PROCESS_BUDGET) &&
           BSP_UART_ReadByte(&g_tas_dtu_uart, &ch))
    {
        processed++;
        g_tas_dtu_status.last_rx_tick = HAL_GetTick();

        if (g_tas_dtu_line_len < (uint16_t)(TAS_DTU_LINE_SIZE - 1U))
        {
            g_tas_dtu_line[g_tas_dtu_line_len++] = (char)ch;
            g_tas_dtu_line[g_tas_dtu_line_len] = '\0';
        }
        else
        {
            g_tas_dtu_line_len = 0U;
            g_tas_dtu_json_depth = 0;
            g_tas_dtu_status.at_error_count++;
            continue;
        }

        if (ch == '{')
        {
            g_tas_dtu_json_depth++;
        }
        else if ((ch == '}') && (g_tas_dtu_json_depth > 0))
        {
            g_tas_dtu_json_depth--;
        }

        if ((g_tas_dtu_json_depth == 0) &&
            (g_tas_dtu_line_len > 0U) &&
            (strchr(g_tas_dtu_line, '{') != NULL) &&
            (strchr(g_tas_dtu_line, '}') != NULL))
        {
            App_TasDtu_HandleLine(g_tas_dtu_line);
            g_tas_dtu_line_len = 0U;
            g_tas_dtu_json_depth = 0;
        }
        else if ((ch == '\r') || (ch == '\n'))
        {
            if (g_tas_dtu_line_len > 0U)
            {
                g_tas_dtu_line[g_tas_dtu_line_len] = '\0';
                App_TasDtu_HandleLine(g_tas_dtu_line);
                g_tas_dtu_line_len = 0U;
                g_tas_dtu_json_depth = 0;
            }
        }
    }

    if (processed >= TAS_DTU_RX_PROCESS_BUDGET)
    {
        taskYIELD();
    }

    App_TasDtu_ProcessRiseQueue();
}

tas_dtu_result_t App_TasDtu_ReportTelemetry(void)
{
    char json[TAS_DTU_JSON_SIZE];
    tas_dtu_result_t result;
    size_t json_len;

    result = App_TasDtu_BuildTelemetryJson(json, sizeof(json), "telemetry");
    if (result != TAS_DTU_RESULT_OK)
    {
        DTU_TRANSFER_LOG_W("DTU", "[DTU] telemetry build failed: %s", App_TasDtu_ResultName(result));
        return result;
    }

    json_len = strlen(json) + 1U;
    if (json_len > TAS_DTU_PACKET_SIZE)
    {
        DTU_TRANSFER_LOG_E("DTU", "[DTU] telemetry exceeds packet limit bytes=%lu limit=%u",
                           (unsigned long)json_len,
                           (unsigned int)TAS_DTU_PACKET_SIZE);
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    return App_TasDtu_SendJson(json);
}

tas_dtu_result_t App_TasDtu_ReportRiseCount(const w25q_rise_event_t *event)
{
    char json[TAS_DTU_JSON_SIZE];
    const char *role;
    int len;

    if (event == NULL)
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    role = (event->role == LIFT_ROLE_MAIN) ? "main" : "sub";
    len = snprintf(json, sizeof(json),
                   "{\"type\":\"rise_count\",\"device\":\"%s\",\"uid\":\"%s\","
                   "\"product_type\":\"large_scissor\",\"event_seq\":%lu,"
                   "\"role\":\"%s\",\"delta\":1,\"rise_ms\":3000,\"offline\":%u,"
                   "\"usage_epoch\":%lu,\"stats\":{\"up\":%lu,\"up_main\":%lu,\"up_sub\":%lu}}",
                   LIFT_IOT_DEVICE_ID,
                   App_TasDtu_ChipUidString(),
                   (unsigned long)event->sequence,
                   role,
                   (unsigned int)event->from_flash,
                   (unsigned long)event->usage_epoch,
                   (unsigned long)event->up_total,
                   (unsigned long)event->up_main,
                   (unsigned long)event->up_sub);
    if ((len < 0) || (len >= (int)sizeof(json)))
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    return App_TasDtu_SendJson(json);
}

tas_dtu_result_t App_TasDtu_ReportHeight(void)
{
    char json[TAS_DTU_JSON_SIZE];
    lift_iot_result_t lr;

    lr = LiftIot_BuildHeightJson(json, sizeof(json), g_tas_dtu_status.tx_seq);
    if (lr != LIFT_IOT_OK)
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    return App_TasDtu_SendJson(json);
}

tas_dtu_result_t App_TasDtu_ReportStatus(const char *event)
{
    char json[TAS_DTU_JSON_SIZE];

    if (event == NULL)
    {
        event = "status";
    }

    if (LiftIot_BuildStatusJson(json,
                                    sizeof(json),
                                    event,
                                    g_tas_dtu_status.tx_seq,
                                    App_TasDtu_StateName(g_tas_dtu_status.state),
                                    g_tas_dtu_status.csq_rssi) != LIFT_IOT_OK)
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    return App_TasDtu_SendJson(json);
}

tas_dtu_result_t App_TasDtu_SendJson(const char *json)
{
    char tx_buf[TAS_DTU_JSON_SIZE + 2U];
    int len;
    tas_dtu_result_t result;

    if (json == NULL)
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    if (App_TasDtu_IsTransparentReady() == 0U)
    {
        return TAS_DTU_RESULT_NOT_READY;
    }

    len = snprintf(tx_buf, sizeof(tx_buf), "%s\n", json);
    if ((len < 0) || (len >= (int)sizeof(tx_buf)))
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    result = App_TasDtu_SendRaw(tx_buf);
    if (result == TAS_DTU_RESULT_OK)
    {
        g_tas_dtu_status.tx_seq++;
        DTU_TRANSFER_LOG_A("DTU", "[DTU] JSON TX seq=%lu bytes=%d", (unsigned long)g_tas_dtu_status.tx_seq, len);
    }
    else
    {
        DTU_TRANSFER_LOG_E("DTU", "[DTU] JSON TX failed: %s", App_TasDtu_ResultName(result));
    }

    return result;
}

uint8_t App_TasDtu_IsTransparentReady(void)
{
    if ((g_tas_dtu_status.state == TAS_DTU_STATE_TRANSPARENT) &&
        (g_tas_dtu_status.mqtt_configured != 0U) &&
        (g_tas_dtu_status.transparent_ready != 0U))
    {
        return 1U;
    }

    return 0U;
}

const tas_dtu_status_t *App_TasDtu_GetStatus(void)
{
    return &g_tas_dtu_status;
}

const char *App_TasDtu_StateName(tas_dtu_state_t state)
{
    switch (state)
    {
        case TAS_DTU_STATE_OFF:
            return "off";

        case TAS_DTU_STATE_UART_READY:
            return "uart_ready";

        case TAS_DTU_STATE_CMD_MODE:
            return "cmd_mode";

        case TAS_DTU_STATE_CONFIGURED:
            return "configured";

        case TAS_DTU_STATE_TRANSPARENT:
            return "transparent";

        case TAS_DTU_STATE_ERROR:
        default:
            return "error";
    }
}

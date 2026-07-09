#include "app_tas_dtu.h"
#include "lift_iot.h"
#include "app_w25qxx.h"
#include "usart.h"
#include "main.h"
#include "elog.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ================= Private Objects ================= */

bsp_uart_t g_tas_dtu_uart;

static uint8_t g_tas_dtu_rx_dma_buf[TAS_DTU_RX_DMA_SIZE];
static tas_dtu_status_t g_tas_dtu_status;
static char g_tas_dtu_line[TAS_DTU_LINE_SIZE];
static uint16_t g_tas_dtu_line_len;
static int16_t g_tas_dtu_json_depth;
static char g_tas_dtu_at_resp[TAS_DTU_AT_RESPONSE_SIZE];

#define TAS_DTU_STM32_UID_BASE_ADDR  (0x1FFF7A10UL)

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
        elog_a("DTU", "[DTU] AT TX: AT+USERPWD=***");
        return;
    }

    elog_a("DTU", "[DTU] AT TX: %s", cmd);
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

    elog_a("DTU",
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
        (void)snprintf(uid, sizeof(uid), "%08lX%08lX%08lX",
                       (unsigned long)uid_reg[0],
                       (unsigned long)uid_reg[1],
                       (unsigned long)uid_reg[2]);
    }

    return uid;
}

static void App_TasDtu_BuildClientId(char *buf, uint16_t size)
{
    const char *uid;
    const char *suffix;
    uint16_t uid_len;

    if ((buf == NULL) || (size == 0U))
    {
        return;
    }

    uid = App_TasDtu_ChipUidString();
    uid_len = (uint16_t)strlen(uid);
    suffix = (uid_len > 12U) ? &uid[uid_len - 12U] : uid;

    (void)snprintf(buf, size, "%s_%s_dtu", LIFT_IOT_DEVICE_ID, suffix);
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

    elog_a("DTU", "[DTU] switch USART3 baud to %lu", (unsigned long)baud);

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
        elog_w("DTU", "[DTU] uart blocking tx failed: len=%u", len);
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

    elog_w("DTU",
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

static char App_TasDtu_ToUpper(char ch)
{
    if ((ch >= 'a') && (ch <= 'z'))
    {
        return (char)(ch - ('a' - 'A'));
    }

    return ch;
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
            dst[out++] = App_TasDtu_ToUpper(*src);
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
        elog_a("DTU", "[DTU] enter command mode: send +++ (attempt %u/3)",
               (unsigned int)(i + 1U));
        result = App_TasDtu_SendRaw("+++");
        if (result != TAS_DTU_RESULT_OK)
        {
            elog_e("DTU", "[DTU] +++ send failed: %s", App_TasDtu_ResultName(result));
            continue;
        }

        (void)App_TasDtu_WaitTxIdle(1000U);
        result = App_TasDtu_WaitFor("OK", 3000U,
                                    g_tas_dtu_at_resp,
                                    (uint16_t)sizeof(g_tas_dtu_at_resp));
        if (result == TAS_DTU_RESULT_OK)
        {
            g_tas_dtu_status.state = TAS_DTU_STATE_CMD_MODE;
            elog_a("DTU", "[DTU] enter command mode ok (attempt %u)",
                   (unsigned int)(i + 1U));
            return TAS_DTU_RESULT_OK;
        }

        /* +++ 没响应，再试一次 AT，判断模块是否本来就在命令模式。 */
        if (App_TasDtu_TestAt() == TAS_DTU_RESULT_OK)
        {
            g_tas_dtu_status.state = TAS_DTU_STATE_CMD_MODE;
            elog_a("DTU", "[DTU] command mode already active");
            return TAS_DTU_RESULT_OK;
        }

        if (i < 2U)
        {
            App_TasDtu_DelayMs(1000U);
        }
    }

    elog_e("DTU", "[DTU] enter command mode failed after 3 attempts");
    App_TasDtu_SetError(TAS_DTU_RESULT_TIMEOUT);
    return TAS_DTU_RESULT_TIMEOUT;
}

static tas_dtu_result_t App_TasDtu_SendConfigCommand(const char *cmd,
                                                     uint32_t timeout_ms)
{
    tas_dtu_result_t result = App_TasDtu_SendCommand(cmd, "OK", timeout_ms);

    if (result != TAS_DTU_RESULT_OK)
    {
        elog_e("DTU", "[DTU] config command failed: %s result=%s",
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

    App_TasDtu_BuildClientId(client_id, sizeof(client_id));

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
                                         TAS_DTU_TOPIC_COMMAND_SUB);
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
    }

    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+MQTTPUB=1,\"%s\",0,0,1,1",
                                         TAS_DTU_TOPIC_TELEMETRY);
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
        elog_a("DTU", "[DTU] MQTT connected");
        return;
    }

    if (App_TasDtu_ContainsIgnoreCase(line, "MQTT CLOSED") ||
        App_TasDtu_ContainsIgnoreCase(line, "MQTT SUB LOST"))
    {
        g_tas_dtu_status.transparent_ready = 0U;
        g_tas_dtu_status.state = TAS_DTU_STATE_CONFIGURED;
        elog_w("DTU", "[DTU] MQTT not ready: %.96s", line);
    }
}

static tas_dtu_result_t App_TasDtu_WaitMqttConnected(uint32_t timeout_ms)
{
    tas_dtu_result_t result;

    result = App_TasDtu_WaitFor("MQTT CONNECTED",
                                timeout_ms,
                                g_tas_dtu_at_resp,
                                (uint16_t)sizeof(g_tas_dtu_at_resp));
    App_TasDtu_LogAtRx(result, "MQTT CONNECTED", g_tas_dtu_at_resp);

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
    elog_a("DTU", "[DTU] ASKCONNECT collect response window");

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
        elog_w("DTU", "[DTU] ATO failed: %s", App_TasDtu_ResultName(result));
    }

    return result;
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

    if (App_TasDtu_ContainsIgnoreCase(g_tas_dtu_at_resp, "MQTT CONNECTED"))
    {
        App_TasDtu_FlushInput();
        result = App_TasDtu_ExitCommandMode();
        if (result != TAS_DTU_RESULT_OK)
        {
            return result;
        }

        g_tas_dtu_status.mqtt_configured = 1U;
        g_tas_dtu_status.transparent_ready = 1U;
        g_tas_dtu_status.state = TAS_DTU_STATE_TRANSPARENT;
        elog_a("DTU", "[DTU] saved MQTT link is connected (MQTT CONNECTED in response)");
        return TAS_DTU_RESULT_OK;
    }

    if (App_TasDtu_AskConnectIsChannel1Ready() == 0U)
    {
        elog_w("DTU", "[DTU] saved MQTT link not connected: %.96s", g_tas_dtu_at_resp);
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
    elog_a("DTU", "[DTU] saved MQTT link is connected");
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

    elog_a("DTU", "[DTU] probe saved MQTT link at %lu", (unsigned long)baud);
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
        elog_a("DTU", "[DTU] poll ASKCONNECT at %lu (%lu/%lu)",
               (unsigned long)baud,
               (unsigned long)(i + 1U),
               (unsigned long)max_polls);

        result = App_TasDtu_QuerySavedMqttLinkAtBaud(baud);
        if (result == TAS_DTU_RESULT_OK)
        {
            return TAS_DTU_RESULT_OK;
        }

        if (result != TAS_DTU_RESULT_NOT_READY)
        {
            elog_w("DTU", "[DTU] poll AT error at %lu: %s",
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

static tas_dtu_result_t App_TasDtu_ReportCommandResult(const char *cmd,
                                                       const char *msg_id,
                                                       const char *result_text,
                                                       const char *event)
{
    char json[TAS_DTU_JSON_SIZE];

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

    if (LiftIot_BuildCommandStatusJson(json,
                                           sizeof(json),
                                           event,
                                           cmd,
                                           msg_id,
                                           result_text,
                                           g_tas_dtu_status.tx_seq,
                                           App_TasDtu_StateName(g_tas_dtu_status.state),
                                           g_tas_dtu_status.csq_rssi) != LIFT_IOT_OK)
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    return App_TasDtu_SendJson(json);
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

static lift_iot_result_t App_TasDtu_ApplyProductTypeCommand(const char *json)
{
    char product_type[24];

    product_type[0] = '\0';
    (void)App_TasDtu_JsonGetString(json, "product_type", product_type, sizeof(product_type));

    if ((product_type[0] == '\0') ||
        (strcmp(product_type, App_Product_TypeName(g_product_type)) != 0))
    {
        return LIFT_IOT_DENIED;
    }

    g_config.product_type = (uint8_t)g_product_type;
    if (App_W25Qxx_Config_Save() != W25Q_OK)
    {
        return LIFT_IOT_DENIED;
    }

    return LIFT_IOT_OK;
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

    /*
     * TAS-LTE-892D_s4 实测 exact topic 下行不稳定，订阅 gaochang/lift/#。
     * 因此必须只处理 Web 命令，忽略本机 telemetry/status 回环。
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

    if (App_TasDtu_JsonHasCommand(json, "ping"))
    {
        (void)App_TasDtu_ReportCommandResult("ping", msg_id, "pong", "pong");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "get_status") ||
        App_TasDtu_JsonHasCommand(json, "report_now"))
    {
        (void)App_TasDtu_ReportTelemetry();
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

    if (App_TasDtu_JsonHasCommand(json, "set_product_type"))
    {
        lift_result = App_TasDtu_ApplyProductTypeCommand(json);
        (void)App_TasDtu_ReportConfigResult("set_product_type",
                                            msg_id,
                                            (lift_result == LIFT_IOT_OK) ? "product_type_set" : "product_type_set_denied",
                                            (lift_result == LIFT_IOT_OK) ? "set_product_type_ok" : "set_product_type_denied");
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
        LiftIot_MaintenanceDone(account);
        (void)App_TasDtu_ReportCommandResult("maintenance_done", msg_id, "maintenance_done", "maintenance_done_ok");
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

    g_tas_dtu_status.mqtt_configured = 0U;
    g_tas_dtu_status.transparent_ready = 0U;

    elog_a("DTU",
           "[DTU] MQTT config start broker=%s:%u pub=%s sub=%s",
           TAS_DTU_BROKER_HOST,
           (unsigned int)TAS_DTU_BROKER_PORT,
           TAS_DTU_TOPIC_TELEMETRY,
           TAS_DTU_TOPIC_COMMAND_SUB);

    result = App_TasDtu_EnterCommandMode();
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        elog_e("DTU", "[DTU] MQTT config failed at command mode: %s", App_TasDtu_ResultName(result));
        return result;
    }

    result = App_TasDtu_ConfigMqttChannel();
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        elog_e("DTU", "[DTU] MQTT config failed: %s", App_TasDtu_ResultName(result));
        return result;
    }

    g_tas_dtu_status.mqtt_configured = 1U;
    g_tas_dtu_status.state = TAS_DTU_STATE_CONFIGURED;

    result = App_TasDtu_SendCommand("AT&W", "OK", 5000U);
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        elog_e("DTU", "[DTU] MQTT config save failed: %s", App_TasDtu_ResultName(result));
        return result;
    }

    elog_a("DTU", "[DTU] MQTT config saved, reboot DTU");
    result = App_TasDtu_SendCommand("AT+CFUN=1,1", "OK", 3000U);
    if ((result != TAS_DTU_RESULT_OK) && (result != TAS_DTU_RESULT_TIMEOUT))
    {
        App_TasDtu_SetError(result);
        elog_e("DTU", "[DTU] MQTT config reboot failed: %s", App_TasDtu_ResultName(result));
        return result;
    }

    g_tas_dtu_status.transparent_ready = 0U;
    g_tas_dtu_status.state = TAS_DTU_STATE_CONFIGURED;
    App_TasDtu_FlushInput();

    elog_a("DTU", "[DTU] wait MQTT connected status");
    result = App_TasDtu_WaitMqttConnected(TAS_DTU_CONFIGURE_WAIT_MS);
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        elog_e("DTU", "[DTU] MQTT connect status timeout: %s", App_TasDtu_ResultName(result));
        return result;
    }

    App_TasDtu_FlushInput();
    elog_a("DTU", "[DTU] MQTT transparent mode ready");
    return TAS_DTU_RESULT_OK;
}

tas_dtu_result_t App_TasDtu_StartMqtt(void)
{
    tas_dtu_result_t result;

    g_tas_dtu_status.mqtt_configured = 0U;
    g_tas_dtu_status.transparent_ready = 0U;

    (void)App_TasDtu_SetUartBaud(TAS_DTU_PRIMARY_BAUD);
    App_TasDtu_FlushInput();

    elog_a("DTU", "[DTU] startup wait MQTT URC (%lus)",
           (unsigned long)TAS_DTU_STARTUP_WAIT_MS / 1000UL);
    result = App_TasDtu_WaitMqttConnected(TAS_DTU_STARTUP_WAIT_MS);
    if (result == TAS_DTU_RESULT_OK)
    {
        App_TasDtu_FlushInput();
        elog_a("DTU", "[DTU] MQTT transparent mode ready");
        return TAS_DTU_RESULT_OK;
    }

    elog_w("DTU", "[DTU] no URC, poll ASKCONNECT at %lu",
           (unsigned long)TAS_DTU_PRIMARY_BAUD);
    result = App_TasDtu_PollMqttLinkAtBaud(TAS_DTU_PRIMARY_BAUD,
                                            TAS_DTU_LINK_POLL_INTERVAL_MS,
                                            TAS_DTU_LINK_POLL_COUNT_9600);
    if (result == TAS_DTU_RESULT_OK)
    {
        return TAS_DTU_RESULT_OK;
    }

    if (result == TAS_DTU_RESULT_NOT_READY)
    {
        elog_w("DTU", "[DTU] poll at %lu still not connected, try %lu",
               (unsigned long)TAS_DTU_PRIMARY_BAUD,
               (unsigned long)TAS_DTU_FALLBACK_BAUD);
        result = App_TasDtu_PollMqttLinkAtBaud(TAS_DTU_FALLBACK_BAUD,
                                                TAS_DTU_LINK_POLL_INTERVAL_MS,
                                                TAS_DTU_LINK_POLL_COUNT_115200);
        if (result == TAS_DTU_RESULT_OK)
        {
            return TAS_DTU_RESULT_OK;
        }
    }

    if (result != TAS_DTU_RESULT_NOT_READY)
    {
        App_TasDtu_SetError(result);
        elog_e("DTU", "[DTU] poll AT error: %s", App_TasDtu_ResultName(result));
        return result;
    }

    elog_w("DTU", "[DTU] all polls NOT_READY, force configure at %lu",
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

    elog_a("DTU", "[DTU] reboot request");

    result = App_TasDtu_EnterCommandMode();
    if (result != TAS_DTU_RESULT_OK)
    {
        App_TasDtu_SetError(result);
        elog_e("DTU", "[DTU] reboot failed at command mode: %s", App_TasDtu_ResultName(result));
        return result;
    }

    result = App_TasDtu_SendCommand("AT+CFUN=1,1", "OK", 3000U);
    App_TasDtu_DelayMs(5000U);
    g_tas_dtu_status.transparent_ready = 0U;
    g_tas_dtu_status.state = TAS_DTU_STATE_CONFIGURED;
    App_TasDtu_FlushInput();

    elog_a("DTU", "[DTU] reboot command result=%s", App_TasDtu_ResultName(result));
    return result;
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
}

tas_dtu_result_t App_TasDtu_ReportTelemetry(void)
{
    char json[TAS_DTU_JSON_SIZE];
    tas_dtu_result_t result;

    result = App_TasDtu_BuildTelemetryJson(json, sizeof(json), "telemetry");
    if (result != TAS_DTU_RESULT_OK)
    {
        return result;
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
        elog_a("DTU", "[DTU] JSON TX seq=%lu bytes=%d", (unsigned long)g_tas_dtu_status.tx_seq, len);
    }
    else
    {
        elog_e("DTU", "[DTU] JSON TX failed: %s", App_TasDtu_ResultName(result));
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

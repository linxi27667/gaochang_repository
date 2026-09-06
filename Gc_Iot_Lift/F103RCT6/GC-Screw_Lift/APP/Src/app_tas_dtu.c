#include "app_tas_dtu.h"
#include "app_lift_iot.h"
#include "app_w25qxx.h"
#include "encoder.h"
#include "key.h"
#include "motor.h"
#include "safety.h"
#include "usart.h"
#include "main.h"
#include "elog.h"
#include "cmsis_os.h"
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

#define TAS_DTU_STM32_UID_BASE_ADDR  (0x1FFFF7E8UL)
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

const char *App_TasDtu_ChipUidString(void)
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
                   App_TasDtu_ChipUidString(), suffix);
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

static void App_TasDtu_DelayMs(uint32_t ms)
{
    if (ms > 0U)
    {
        osDelay(ms);
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

    if ((g_tas_dtu_status.current_baud == baud) && (huart1.Init.BaudRate == baud))
    {
        return TAS_DTU_RESULT_OK;
    }

    elog_a("DTU", "[DTU] switch USART1 baud to %lu", (unsigned long)baud);

    (void)HAL_UART_AbortReceive(&huart1);
    (void)HAL_UART_AbortTransmit(&huart1);
    (void)HAL_UART_DeInit(&huart1);

    huart1.Init.BaudRate = baud;
    if (HAL_UART_Init(&huart1) != HAL_OK)
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

static uint8_t App_TasDtu_IsHexChar(char ch)
{
    ch = App_TasDtu_ToLower(ch);
    if (((ch >= '0') && (ch <= '9')) || ((ch >= 'a') && (ch <= 'f')))
    {
        return 1U;
    }
    return 0U;
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

static tas_dtu_result_t App_TasDtu_WaitFor(const char *expect,
                                           uint32_t timeout_ms,
                                           char *resp,
                                           uint16_t resp_size)
{
    uint32_t start;
    uint16_t len;
    uint16_t total_rx;

    if ((expect == NULL) || (resp == NULL) || (resp_size < 2U))
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    resp[0] = '\0';
    len = 0U;
    total_rx = 0U;
    start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint8_t ch;

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
     * 涓婄數鍚庢ā鍧楅€氬父宸插湪閫忎紶鎬侊紝鍏堝彂 AT 浼氳褰撲綔涓氬姟鏁版嵁鍙戝埌 MQTT銆?
     * 鍥犳鍥哄畾鍏堟寜鎵嬪唽鐢?+++ 杩涘叆鍛戒护妯″紡锛涜嫢 +++ 澶辫触锛屽啀鐢?AT 鍒ゆ柇鏄惁鏈潵灏卞湪鍛戒护妯″紡銆?
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

        /* +++ 娌″搷搴旓紝鍐嶈瘯涓€娆?AT锛屽垽鏂ā鍧楁槸鍚︽湰鏉ュ氨鍦ㄥ懡浠ゆā寮忋€?*/
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

    result = App_TasDtu_SendConfigFormat(3000U,
                                         "AT+MQTTKEEP=%u,1",
                                         (unsigned int)TAS_DTU_MQTT_KEEPALIVE_SEC);
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

    if ((resp == NULL) || (resp_size < 2U))
    {
        return TAS_DTU_RESULT_PARAM_ERROR;
    }

    resp[0] = '\0';
    len = 0U;
    total_rx = 0U;
    start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint8_t ch;

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
     * 涓嶈兘鍙瓑 OK銆傚疄娴嬫ā鍧楀彲鑳藉厛鍚?OK锛岄殢鍚庢墠鍚?+ASKCONNECT锛?
     * 濡傛灉鎻愬墠杩斿洖浼氭妸鐪熷疄杩炴帴鐘舵€侀仐鐣欏埌涓嬩竴鏉?AT 鍝嶅簲閲屻€?
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

static uint8_t App_TasDtu_QueryContains(const char *cmd,
                                        const char *expected,
                                        const char *label)
{
    tas_dtu_result_t result = App_TasDtu_SendCommand(cmd, "OK", 3000U);

    if (result != TAS_DTU_RESULT_OK)
    {
        elog_w("DTU", "[DTU] saved config query failed field=%s result=%s",
               label, App_TasDtu_ResultName(result));
        return 0U;
    }

    if (strstr(g_tas_dtu_at_resp, expected) == NULL)
    {
        elog_w("DTU", "[DTU] saved config mismatch field=%s expected=%s resp=%.160s",
               label, expected, g_tas_dtu_at_resp);
        return 0U;
    }

    return 1U;
}

static uint8_t App_TasDtu_SavedMqttConfigMatches(void)
{
    char broker_port[8];
    char client_id[80];
    char topic_up[96];
    char topic_down[96];

    (void)snprintf(broker_port, sizeof(broker_port), "%u", (unsigned int)TAS_DTU_BROKER_PORT);
    App_TasDtu_BuildClientId(client_id, sizeof(client_id));
    App_TasDtu_BuildV1Topic(topic_up, sizeof(topic_up), "up");
    App_TasDtu_BuildV1Topic(topic_down, sizeof(topic_down), "down");

    if ((App_TasDtu_QueryContains("AT+IPPORT?", TAS_DTU_BROKER_HOST, "broker") == 0U) ||
        (strstr(g_tas_dtu_at_resp, broker_port) == NULL) ||
        (App_TasDtu_QueryContains("AT+CLIENTID?", client_id, "client_id") == 0U) ||
        (App_TasDtu_QueryContains("AT+MQTTPUB?", topic_up, "publish_topic") == 0U) ||
        (App_TasDtu_QueryContains("AT+MQTTSUB?", topic_down, "subscribe_topic") == 0U))
    {
        return 0U;
    }

    return 1U;
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
        if (App_TasDtu_SavedMqttConfigMatches() == 0U)
        {
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

    if (App_TasDtu_SavedMqttConfigMatches() == 0U)
    {
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
    if (App_LiftIot_BuildTelemetryJson(buf,
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
        (strstr(result_text, "unknown") != NULL) ||
        (strstr(result_text, "missing") != NULL))
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

    (void)snprintf(dst, dst_size, "%s", (src != NULL) ? src : "");
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
        g_tas_dtu_cmd_cache_next = (uint8_t)((g_tas_dtu_cmd_cache_next + 1U) % TAS_DTU_MSG_ID_CACHE_SIZE);
    }

    App_TasDtu_CopyCacheString(g_tas_dtu_cmd_cache[slot].msg_id,
                                (uint16_t)sizeof(g_tas_dtu_cmd_cache[slot].msg_id), msg_id);
    App_TasDtu_CopyCacheString(g_tas_dtu_cmd_cache[slot].cmd,
                                (uint16_t)sizeof(g_tas_dtu_cmd_cache[slot].cmd), cmd);
    App_TasDtu_CopyCacheString(g_tas_dtu_cmd_cache[slot].result_text,
                                (uint16_t)sizeof(g_tas_dtu_cmd_cache[slot].result_text), result_text);
    App_TasDtu_CopyCacheString(g_tas_dtu_cmd_cache[slot].event,
                                (uint16_t)sizeof(g_tas_dtu_cmd_cache[slot].event), event);
}

static tas_dtu_result_t App_TasDtu_ReportCommandResult(const char *cmd,
                                                       const char *msg_id,
                                                       const char *result_text,
                                                       const char *event)
{
    char json[TAS_DTU_JSON_SIZE];
    const char *wire_result;

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

    wire_result = App_TasDtu_V1CommandResult(result_text);

    if (App_LiftIot_BuildCommandStatusJson(json,
                                           sizeof(json),
                                           event,
                                           cmd,
                                           msg_id,
                                           wire_result,
                                           g_tas_dtu_status.tx_seq,
                                           App_TasDtu_StateName(g_tas_dtu_status.state),
                                           g_tas_dtu_status.csq_rssi) != LIFT_IOT_OK)
    {
        return TAS_DTU_RESULT_BUFFER_SMALL;
    }

    App_TasDtu_SaveCommandResultCache(cmd, msg_id, result_text, event);
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

static uint8_t App_TasDtu_IsAcceptedCommandJson(const char *json)
{
    char type[16];
    char target_uid[64];

    if (json == NULL)
    {
        return 0U;
    }

    type[0] = '\0';
    target_uid[0] = '\0';
    (void)App_TasDtu_JsonGetString(json, "type", type, sizeof(type));
    (void)App_TasDtu_JsonGetString(json, "target_uid", target_uid, sizeof(target_uid));

    /*
     * v1 涓嬭涓婚鎸夎姱鐗?UID 闅旂锛涗粛鏍￠獙 payload锛岄伩鍏嶉€忔槑浼犺緭
     * 鍥炵幆鎴栭敊璇富棰樼殑 JSON 瑙﹀彂鏈満鎺у埗鍛戒护銆?
     */
    if (strcmp(type, "command") != 0)
    {
        return 0U;
    }

    return ((target_uid[0] != '\0') &&
            (App_TasDtu_UidMatchesLocal(target_uid) != 0U)) ? 1U : 0U;
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

    if (msg_id[0] == '\0')
    {
        elog_w("DTU", "[DTU] command rejected: missing msg_id cmd=%s",
               (cmd_name[0] != '\0') ? cmd_name : "unknown");
        (void)App_TasDtu_ReportCommandResult((cmd_name[0] != '\0') ? cmd_name : "unknown",
                                             "", "missing_msg_id", "command_rejected");
        return;
    }

    if (App_TasDtu_LoadCachedCommandResult(msg_id,
                                            cached_cmd, sizeof(cached_cmd),
                                            cached_result, sizeof(cached_result),
                                            cached_event, sizeof(cached_event)) != 0U)
    {
        elog_a("DTU", "[DTU] command duplicate: cmd=%s msg_id=%s", cached_cmd, msg_id);
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
        (void)App_TasDtu_ReportTelemetry();
        (void)App_TasDtu_ReportCommandResult("get_status", msg_id, "reported", "report_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "lock"))
    {
        (void)App_LiftIot_SetLocked(1U, account);
        (void)App_TasDtu_ReportCommandResult("lock", msg_id, "locked", "lock_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "unlock"))
    {
        (void)App_LiftIot_SetLocked(0U, account);
        (void)App_TasDtu_ReportCommandResult("unlock", msg_id, "unlocked", "unlock_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "admin_enter"))
    {
        password[0] = '\0';
        (void)App_TasDtu_JsonGetString(json, "password", password, sizeof(password));
        lift_result = App_LiftIot_EnterAdmin(password, account);
        (void)App_TasDtu_ReportCommandResult("admin_enter",
                                             msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "admin_entered" : "admin_denied",
                                             (lift_result == LIFT_IOT_OK) ? "admin_enter_ok" : "admin_enter_denied");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "admin_exit"))
    {
        App_LiftIot_ExitAdmin();
        (void)App_TasDtu_ReportCommandResult("admin_exit", msg_id, "admin_exited", "admin_exit_ok");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "fault_clear"))
    {
        lift_result = App_LiftIot_ClearFault(account);
        (void)App_TasDtu_ReportCommandResult("fault_clear",
                                             msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "fault_cleared" : "fault_clear_denied",
                                             (lift_result == LIFT_IOT_OK) ? "fault_clear_ok" : "fault_clear_denied");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "clear_alarm"))
    {
        lift_result = App_LiftIot_ClearFault(account);
        (void)App_TasDtu_ReportCommandResult("clear_alarm",
                                             msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "alarm_cleared" : "alarm_clear_denied",
                                             (lift_result == LIFT_IOT_OK) ? "clear_alarm_ok" : "clear_alarm_denied");
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

        lift_result = App_LiftIot_AdminJog(column_index, direction_up, duration_ms, account);
        (void)App_TasDtu_ReportCommandResult("admin_jog",
                                             msg_id,
                                             (lift_result == LIFT_IOT_OK) ? "admin_jog_ok" : "admin_jog_denied",
                                             (lift_result == LIFT_IOT_OK) ? "admin_jog_ok" : "admin_jog_denied");
        return;
    }

    if (App_TasDtu_JsonHasCommand(json, "maintenance_done"))
    {
        App_LiftIot_MaintenanceDone(account);
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
                      &huart1,
                      g_tas_dtu_rx_dma_buf,
                      TAS_DTU_RX_DMA_SIZE) != BSP_UART_OK)
    {
        App_TasDtu_SetError(TAS_DTU_RESULT_UART_ERROR);
        return TAS_DTU_RESULT_UART_ERROR;
    }

    BSP_UART_SetDirection(&g_tas_dtu_uart, NULL, 0); /* F103: no RS485 DE */

    memset(&g_tas_dtu_status, 0, sizeof(g_tas_dtu_status));
    memset(g_tas_dtu_cmd_cache, 0, sizeof(g_tas_dtu_cmd_cache));
    g_tas_dtu_cmd_cache_next = 0U;
    g_tas_dtu_status.state = TAS_DTU_STATE_UART_READY;
    g_tas_dtu_status.csq_rssi = -1;
    g_tas_dtu_status.current_baud = huart1.Init.BaudRate;
    App_LiftIot_Init();
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

    while (BSP_UART_ReadByte(&g_tas_dtu_uart, &ch))
    {
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

    lr = App_LiftIot_BuildHeightJson(json, sizeof(json), g_tas_dtu_status.tx_seq);
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

    if (App_LiftIot_BuildStatusJson(json,
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

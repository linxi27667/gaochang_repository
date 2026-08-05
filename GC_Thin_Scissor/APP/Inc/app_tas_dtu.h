#ifndef __APP_TAS_DTU_H__
#define __APP_TAS_DTU_H__

#include <stdint.h>
#include "main.h"
#include "bsp_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TAS_DTU_BROKER_HOST
#define TAS_DTU_BROKER_HOST              "8.134.201.118"
#endif

#ifndef TAS_DTU_BROKER_PORT
#define TAS_DTU_BROKER_PORT              1883U
#endif
#define TAS_DTU_CLIENT_ID                "gc-lift-{chip_uid}"
#define TAS_DTU_MQTT_USERNAME            ""
#define TAS_DTU_MQTT_PASSWORD            ""

#define TAS_DTU_TOPIC_TELEMETRY          "gaochang/lift/v1/devices/{chip_uid}/up"
#define TAS_DTU_TOPIC_COMMAND            "gaochang/lift/v1/devices/{chip_uid}/down"
#define TAS_DTU_TOPIC_COMMAND_SUB        "gaochang/lift/v1/devices/{chip_uid}/down"
#define TAS_DTU_TOPIC_STATUS             "gaochang/lift/v1/devices/{chip_uid}/up"

#define TAS_DTU_AT_PASSWORD              "usr.cn#"

#define TAS_DTU_PRIMARY_BAUD             9600U
#define TAS_DTU_FALLBACK_BAUD            115200U

#define TAS_DTU_STARTUP_WAIT_MS           3000U
#define TAS_DTU_LINK_POLL_INTERVAL_MS    15000U
#define TAS_DTU_LINK_POLL_COUNT_9600     6U
#define TAS_DTU_LINK_POLL_COUNT_115200   2U
#define TAS_DTU_CONFIGURE_WAIT_MS        180000U

#define TAS_DTU_RX_DMA_SIZE              512U
#define TAS_DTU_RX_PROCESS_BUDGET        128U
#define TAS_DTU_LINE_SIZE                1024U
#define TAS_DTU_JSON_SIZE                1024U
#define TAS_DTU_PACKET_SIZE              1024U
#define TAS_DTU_AT_RESPONSE_SIZE         768U

typedef enum
{
    TAS_DTU_STATE_OFF = 0,
    TAS_DTU_STATE_UART_READY,
    TAS_DTU_STATE_CMD_MODE,
    TAS_DTU_STATE_CONFIGURED,
    TAS_DTU_STATE_TRANSPARENT,
    TAS_DTU_STATE_ERROR,
} tas_dtu_state_t;

typedef enum
{
    TAS_DTU_RESULT_OK = 0,
    TAS_DTU_RESULT_PARAM_ERROR,
    TAS_DTU_RESULT_UART_ERROR,
    TAS_DTU_RESULT_TIMEOUT,
    TAS_DTU_RESULT_AT_ERROR,
    TAS_DTU_RESULT_NOT_READY,
    TAS_DTU_RESULT_BUFFER_SMALL,
} tas_dtu_result_t;

typedef struct
{
    tas_dtu_state_t state;
    uint8_t mqtt_configured;
    uint8_t transparent_ready;
    int16_t csq_rssi;
    uint32_t tx_seq;
    uint32_t rx_json_count;
    uint32_t at_error_count;
    uint32_t timeout_count;
    uint32_t last_rx_tick;
    uint32_t last_tx_tick;
    uint32_t last_error_tick;
    uint32_t current_baud;
    uint16_t last_rx_bytes;
} tas_dtu_status_t;

extern bsp_uart_t g_tas_dtu_uart;

tas_dtu_result_t App_TasDtu_Init(void);
tas_dtu_result_t App_TasDtu_StartMqtt(void);
tas_dtu_result_t App_TasDtu_ConfigureMqtt(void);
tas_dtu_result_t App_TasDtu_Reboot(void);

void App_TasDtu_ProcessRx(void);
tas_dtu_result_t App_TasDtu_ReportTelemetry(void);
tas_dtu_result_t App_TasDtu_ReportHeight(void);
tas_dtu_result_t App_TasDtu_ReportStatus(const char *event);
tas_dtu_result_t App_TasDtu_SendJson(const char *json);
uint8_t App_TasDtu_IsTransparentReady(void);

const tas_dtu_status_t *App_TasDtu_GetStatus(void);
const char *App_TasDtu_StateName(tas_dtu_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TAS_DTU_H__ */

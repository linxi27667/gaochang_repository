/**
 * @file app_io_map.h
 * @brief Central IO naming map for lift inputs, lift outputs, and platform GPIO.
 *
 * This file documents the logical signal names used by APP/Driver code. SPI
 * and UART alternate-function pins are registered here for traceability, but
 * their electrical setup remains owned by CubeMX-generated init functions.
 */
#ifndef __APP_IO_MAP_H__
#define __APP_IO_MAP_H__

#include <stdint.h>
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IO_IN_UP_BUTTON = 0,     /* PE0: 上升按钮，输入上拉，按下接地，active-low */
    IO_IN_DOWN_BUTTON,       /* PE1: 下降按钮，输入上拉，按下接地，active-low */
    IO_IN_LOCK_BUTTON,       /* PE2: 锁定按钮，输入上拉，按下接地，active-low */
    IO_IN_ESTOP,             /* PE3: 急停 NC，输入上拉，断开/急停读 1，active-high */
    IO_IN_UPPER_LIMIT,       /* PE4: upper limit, pull-up idle high, touched pulls low active */
    IO_IN_REFILL_BUTTON,     /* PE3: refill button, input pull-up, pressed to GND, active-low */
    IO_IN_PHOTOELECTRIC,     /* PE8: photo input, active-high alarm */
    IO_IN_LOWER_LIMIT,       /* PE6: lower limit, active-high; masks photo only */
    IO_IN_MAX
} io_in_id_t;

typedef enum {
    IO_OUT_MOTOR = 0,        /* PF8: 电机继电器，输出下拉，默认低，高电平吸合 */
    IO_OUT_DROP_VALVE,       /* PF9: 下降阀继电器，输出下拉，默认低，高电平吸合 */
    IO_OUT_AIR_VALVE,        /* PD8: 气阀输出，输出下拉，默认低，高电平打开 */
    IO_OUT_MAX
} io_out_id_t;

typedef enum {
    IO_PLATFORM_W25Q_CS = 0, /* PB6: W25Q 片选，默认高，高电平不选中 */
    IO_PLATFORM_RS485_DIR,   /* PD0: RS485 方向，默认低，低接收，高发送 */
    IO_PLATFORM_LED_RUN,     /* PG2: Run LED，默认高，低电平点亮 */
    IO_PLATFORM_LED_COM,     /* PG3: Com LED，默认高，低电平点亮 */
    IO_PLATFORM_LED_POWER,   /* PG4: Power LED，默认高，低电平点亮 */
    IO_PLATFORM_MAX
} io_platform_id_t;

typedef enum {
    IO_LED_RUN = 0,          /* PG2: 运行指示灯，低电平点亮 */
    IO_LED_COM,              /* PG3: 通讯指示灯，低电平点亮 */
    IO_LED_POWER,            /* PG4: 电源指示灯，低电平点亮 */
    IO_LED_MAX
} io_led_id_t;

typedef enum {
    IO_AF_SPI1_SCK = 0,      /* PB3: SPI1 SCK，AF5，由 MX_SPI1_Init 初始化 */
    IO_AF_SPI1_MISO,         /* PB4: SPI1 MISO，AF5，由 MX_SPI1_Init 初始化 */
    IO_AF_SPI1_MOSI,         /* PB5: SPI1 MOSI，AF5，由 MX_SPI1_Init 初始化 */
    IO_AF_USART3_TX,         /* PB10: USART3 TX，AF7，由 MX_USART3_UART_Init 初始化 */
    IO_AF_USART3_RX,         /* PB11: USART3 RX，AF7，上拉，由 MX_USART3_UART_Init 初始化 */
    IO_AF_USART6_TX,         /* PC6: USART6 TX，AF8，由 MX_USART6_UART_Init 初始化 */
    IO_AF_USART6_RX,         /* PC7: USART6 RX，AF8，由 MX_USART6_UART_Init 初始化 */
    IO_AF_MAX
} io_af_id_t;

typedef struct {
    const char *signal;
    const char *pin_name;
    uint8_t alternate;
} io_af_info_t;

void App_IO_Map_Init(product_type_t type);
void App_IO_LogSnapshot(const char *reason);

uint8_t App_IO_Read(io_in_id_t id);
uint8_t App_IO_Read_Raw(io_in_id_t id);

void App_IO_Write(io_out_id_t id, uint8_t value);
uint8_t App_IO_Read_Output(io_out_id_t id);
void App_IO_All_Off(void);

void App_IO_PlatformWrite(io_platform_id_t id, uint8_t level);
uint8_t App_IO_PlatformRead(io_platform_id_t id);

void App_IO_W25Q_Select(void);
void App_IO_W25Q_Deselect(void);

void App_IO_RS485_SetTx(void);
void App_IO_RS485_SetRx(void);

void App_IO_LedOn(io_led_id_t id);
void App_IO_LedOff(io_led_id_t id);
void App_IO_LedToggle(io_led_id_t id);

const io_af_info_t *App_IO_GetAFInfo(io_af_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __APP_IO_MAP_H__ */

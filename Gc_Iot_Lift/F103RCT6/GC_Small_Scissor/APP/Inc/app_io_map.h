/**
 * @file app_io_map.h
 * @brief F103RCT6 小剪 IO 逻辑名（光耦低有效输入，归一化 1=有效）
 */
#ifndef __APP_IO_MAP_H__
#define __APP_IO_MAP_H__

#include <stdint.h>
#include "app_product.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IO_IN_UP_BUTTON = 0,     /* IN0: 上升 */
    IO_IN_DOWN_BUTTON,       /* IN1: 下降 */
    IO_IN_LOCK_BUTTON,       /* IN2: 锁定 */
    IO_IN_ESTOP,             /* IN3: 急停 */
    IO_IN_UPPER_LIMIT,       /* IN5: 上限 */
    IO_IN_REFILL_BUTTON,     /* IN4: 补油 */
    IO_IN_PHOTOELECTRIC,     /* IN7: 光电 */
    IO_IN_LOWER_LIMIT,       /* IN6: 下限 */
    IO_IN_MAX
} io_in_id_t;

typedef enum {
    IO_OUT_MOTOR = 0,        /* RELAY0: 电机继电器 */
    IO_OUT_DROP_VALVE,       /* OUT0: 下降阀(24V) */
    IO_OUT_AIR_VALVE,        /* OUT1: 气阀(24V) */
    IO_OUT_MAX
} io_out_id_t;

typedef enum {
    IO_PLATFORM_LED_RUN = 0, /* PC13 Run LED */
    IO_PLATFORM_MAX
} io_platform_id_t;

typedef enum {
    IO_LED_RUN = 0,
    IO_LED_COM,              /* F103 无独立 COM 灯，软状态 */
    IO_LED_POWER,            /* F103 无独立 Power 灯，软状态 */
    IO_LED_MAX
} io_led_id_t;

typedef enum {
    IO_AF_USART1_TX = 0,
    IO_AF_USART1_RX,
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
